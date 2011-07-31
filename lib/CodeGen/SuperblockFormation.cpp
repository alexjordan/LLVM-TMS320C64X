//===-- llvm/CodeGen/SuperblockFormation.cpp --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Perform a formation of superblocks.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "superblock-formation"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineSuperBlock.h"
#include "llvm/CodeGen/SuperblockFormation.h"
#include "llvm/CodeGen/MachinePathProfileBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

STATISTIC(NumSuperBlocksStat, "Number of superblocks created");
STATISTIC(NumDuplicatedBlocksStat, "Number of duplicated basic blocks");

static cl::opt<unsigned>
ExecThresh("exec-freq-thresh",
  cl::desc("Profile info execution threshold for superblocks"),
  cl::init(1), cl::Hidden);

//----------------------------------------------------------------------------

char SuperblockFormation::ID = 0;

INITIALIZE_PASS_BEGIN(SuperblockFormation, "superblock-formation",
                "Profile Guided Superblock Formation", false, false)
INITIALIZE_PASS_DEPENDENCY(MachinePathProfileBuilder)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(SuperblockFormation, "superblock-formation",
                "Profile Guided Superblock Formation", false, false)

//----------------------------------------------------------------------------

FunctionPass *llvm::createSuperblockFormationPass() {
  return new SuperblockFormation();
}

//----------------------------------------------------------------------------

SuperblockFormation::SuperblockFormation()
: MachineFunctionPass(ID)
{
  initializeSuperblockFormationPass(*PassRegistry::getPassRegistry());
}

//----------------------------------------------------------------------------

SuperblockFormation::~SuperblockFormation() { clearSuperblockSet(); }

//----------------------------------------------------------------------------

void SuperblockFormation::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachinePathProfileBuilder>();
  AU.addRequired<MachineDominatorTree>();
  AU.addRequired<MachineLoopInfo>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

//----------------------------------------------------------------------------

void SuperblockFormation::clearSuperblockSet() {
  while (superBlocks.size()) {
    MachineSuperBlock *MSB = *(superBlocks.begin());
    superBlocks.erase(superBlocks.begin());
    delete MSB;
  }
}

//----------------------------------------------------------------------------

const MachineSuperBlockSetTy &SuperblockFormation::getSuperblocks() const {
  return superBlocks;
}

//----------------------------------------------------------------------------

bool SuperblockFormation::isDuplicable(const MachineBasicBlock &MBB) const {

  unsigned numInstructions = 0;

  for (MachineBasicBlock::const_iterator
       MI = MBB.begin(), ME = MBB.end(); MI != ME; ++MI)
  {
    // ignore phi instructions as well as debug values
    if (MI->isPHI() || MI->isDebugValue()) continue;

    // for now, dont allow inline asm
    if (MI->isInlineAsm()) return false;

    // usually duplicating call instructions are less profitable due to their
    // code expansion and restrictions, thus we skip calls for the time being
    if (MI->getDesc().isCall()) return false;

    // some targets use implicitly a dedicated physical register for returns,
    // Also, an expansion of a return may be too big and less profitable when
    // duplicated during pre-reg allocation
    if (MI->getDesc().isReturn()) return false;

    // don't allow any instructions with potential side effects
    if (MI->getDesc().hasUnmodeledSideEffects()) return false;

    // skip instructions that can not be safely duplicated
    if (MI->getDesc().isNotDuplicable()) return false;

    ++numInstructions;
  }

  // for now just assume the duplication to be profitable as long the block
  // is not dead. This can be changed later to be more sophisticated, TODO !
  return numInstructions > 0;
}

//----------------------------------------------------------------------------

bool SuperblockFormation::isAttachableTo(MachineBasicBlock *MBB,
                                         MBBListTy &SB) const
{
  // profile info broken ?
  if (!MBB) return false;

  // basic block has already been processed, we have to skip this basic block
  // in order to avoid overlapping superblock regions. I.e. superblocks that
  // are identified/created by the pass do not share any basic blocks
  if (processedBlocks.count(MBB)) return false;

  // avoid trivial cycles, i.e. single block loops for now. However, this may
  // be a subject for a further region enlargement later (unrolling/peeling)
  if (MBB->isSuccessor(MBB)) return false;

  if (SB.size()) {

    // check for instructions in the basic block, that may potentially have
    // any side-effects (inline asm, or instruction with unmodeled effects)
    if (!isDuplicable(*MBB)) return false;

    // happens, if a loop-header appears in the middle/at the end of the trace.
    // We can not include it since it would destroy the natural ordering and
    // be senseless, since the header is always scheduled before the loop-body
    if (MDT->dominates(MBB, *(SB.begin()))) return false;

    // an other thing that we exclude currently is a header of another loop
    if (MLI->isLoopHeader(MBB)) return false;
  }

  // now iterate over existing successors (at least one of them does exist),
  // and check whether they introduce cycles in the current superblock
  for (MachineBasicBlock::succ_iterator SI = MBB->succ_begin(),
       SE = MBB->succ_end(); SI != SE; ++SI)
  {
    // backedges within superblocks are allowed to the head block only, if a
    // successor is already contained in the superblock, it must be the head,
    // if not, the block is skipped
    MBBListTy::iterator F = std::find(SB.begin(), SB.end(), *SI);
    if (SB.size() && (F != SB.end() && F != SB.begin())) return false;
  }

  return true;
}

//----------------------------------------------------------------------------

unsigned
SuperblockFormation::getPHISourceRegIndex(const MachineInstr &PHIInstr,
                                          MachineBasicBlock *sourceMBB) const
{
  assert(PHIInstr.isPHI() && "Can only process PHI-instructions!");

  // look for the operand index for the register corresponding to the speci-
  // fied machine basic block within the specified machine instruction
  for (unsigned I = 1; I < PHIInstr.getNumOperands(); I += 2)
    if (PHIInstr.getOperand(I + 1).getMBB() == sourceMBB) return I;
  return 0;
}

//----------------------------------------------------------------------------

bool SuperblockFormation::isDefLiveOut(const MachineBasicBlock &MBB,
                                       unsigned reg) const
{
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  // check for any uses outside of the parent MBB
  MachineRegisterInfo::use_iterator UI;
  for (UI = MRI.use_begin(reg); UI != MRI.use_end(); ++UI)
    if ((*UI).getParent() != &MBB)  return true;

  return false;
}

//----------------------------------------------------------------------------

void SuperblockFormation::addSSAUpdateEntry(unsigned oldReg,
                                            unsigned newReg,
                                            MachineBasicBlock *MBB)
{
  // check whether we already have an entry for oldReg in the ssa-update map
  DenseMap<unsigned, ValueVectorTy>::iterator I = SSAUpdateVals.find(oldReg);
  
  // if there is is an entry already, just append a new value to the entry
  if (I != SSAUpdateVals.end()) I->second.push_back(MBBValPairTy(MBB, newReg));
  else {

    // if there is no entry for oldReg yet, create one first. Attach the new
    // value to it, then associate the entire list of available values with
    // oldReg and update the list of registers to consider for the SSA update
    ValueVectorTy newValues;

    newValues.push_back(MBBValPairTy(MBB, newReg));
    SSAUpdateVals.insert(std::make_pair(oldReg, newValues));
    SSAUpdateVirtRegs.push_back(oldReg);
  }
}

//----------------------------------------------------------------------------

void SuperblockFormation::updatePredInfo(MachineBasicBlock *origMBB,
                                         MachineBasicBlock *cloneMBB,
                                         MachineBasicBlock *tracePred,
                                         MachineBasicBlock *clonePred,
                                         DenseMap<unsigned, unsigned> &VRMap)
{
  assert(origMBB && cloneMBB && tracePred && "Bad MBB, can't update preds!");

  /// First of all, scan over the current basic block of the superblock. We
  /// look for phi-instructions and change/update/remove the entry for each
  /// predecessor that presents a side-entry into the block (i.e. all preds
  /// except the one that preceeds in the superblock). We need to do this,
  /// because all side-entries (i.e. preds) will later be directed to enter
  /// the clones instead of the superblock

  MachineBasicBlock::iterator MI;
  for (MI = origMBB->begin(); MI != origMBB->end(); ++MI) {
    if (!MI->isPHI()) continue;

    /// in case we are dealing with a phi-instruction, for each "side entry"
    /// predecessor we need to remove the source in this instruction, since
    /// these preds will be changed to preceed the clone instead of the orig

    DEBUG(dbgs() << "Processing orig. MBB PHI-instruction" << *MI << '\n');

    MachineBasicBlock::pred_iterator PI;
    for (PI = origMBB->pred_begin(); PI != origMBB->pred_end(); ++PI) {
      if (*PI == tracePred) continue;

      // find the operand source within the phi-instruction
      const unsigned predIndex = getPHISourceRegIndex(*MI, *PI);
      assert(predIndex && "Bad PHI-operand-index for the predecessor!");

      // entries for phi instructions must be pairs of reg/block values
      assert(MI->getOperand(predIndex + 1).isMBB() && "Bad MBB operand!");
      assert(MI->getOperand(predIndex).isReg() && "Bad register operand!");

      DEBUG(
        dbgs() << "  removed: " << MI->getOperand(predIndex + 1) << '\n';
        dbgs() << "  removed: " << MI->getOperand(predIndex) << '\n');

      MI->RemoveOperand(predIndex + 1); // remove entry for side-entry-mbb
      MI->RemoveOperand(predIndex);     // remove entry for side-entry-reg

      // if the instruction has no sources, remove entirely
      if (MI->getNumOperands() == 1) MI->eraseFromParent();
    }
  }

  /// now we update all predecessors of the original machine block. This step
  /// effectively removes any side entries from the original superblock and
  /// directs them to the cloned tail block

  DEBUG(dbgs() << "Transfering predecessors from MBB to the clone\n");

  SmallVector<MachineBasicBlock*, 64> Preds(
    origMBB->pred_begin(), origMBB->pred_end());

  for (unsigned PI = 0; PI < Preds.size(); ++PI)
    if (Preds[PI] != tracePred) {
      // replace origMBB by cloneMBB in the successor-info
      Preds[PI]->ReplaceUsesOfBlockWith(origMBB, cloneMBB);
      Preds[PI]->updateTerminator();
      DEBUG(dbgs() << "  " << Preds[PI]->getName() << '\n');
    }

  // the edge within the original superblock trace must not be broken
  assert(tracePred->isSuccessor(origMBB) && "Broken trace pred-edge!");

  /// after the preds have been now corrected to enter the cloned blocks and
  /// the phi-entries for the superblock have been cleaned up, we now need to
  /// clean up the phi-instructions for the cloned blocks as well

  for (MI = cloneMBB->begin(); MI != cloneMBB->end(); ++MI) {
    if (!MI->isPHI()) continue;

    // look for the phi source entry for the original predecessor
    const unsigned predIndex = getPHISourceRegIndex(*MI, tracePred);
    if (!predIndex) continue;

    // entries for phi instructions must be pairs of reg/block values
    assert(MI->getOperand(predIndex + 1).isMBB() && "Bad MBB operand!");
    assert(MI->getOperand(predIndex).isReg() && "Bad register operand!");

    DEBUG(dbgs() << "Processing clone's PHI-instruction\n");

    /// if the current block is not the first one in the tail, then there
    /// is a block (cloned earlier) preceeding it. Since we have added an
    /// additional phi-entry to each successor in the previous step, we can
    /// simply drop the entry for the tracePred here
    MI->RemoveOperand(predIndex + 1);
    MI->RemoveOperand(predIndex);
    if (MI->getNumOperands() == 1) MI->eraseFromParent();
  }
}

//----------------------------------------------------------------------------

void SuperblockFormation::updateSuccInfo(MachineBasicBlock *origMBB,
                                         MachineBasicBlock *cloneMBB,
                                   DenseMap<unsigned, unsigned> &VRMap)
{
  assert(origMBB && cloneMBB && "Bad MBB, can't update successors!");

  /// There is no need for us to patch cfg-edges or correct pred/succ rela-
  /// tions. What we do here, is to adjust the phi-instructions of the succs
  /// blocks since each of them has an additional edge from the cloned blocks.
  /// To simplify matters, we add a new additional phi-entry to each succes-
  /// sor of the original basic block. This is surely not the most efficient
  /// way, however, it simplifies implementation greatly

  MachineBasicBlock::succ_iterator SI;
  for (SI = origMBB->succ_begin(); SI != origMBB->succ_end(); ++SI) {
    DEBUG(dbgs() << "Processing succ: " << (*SI)->getName() << '\n');

    MachineBasicBlock::iterator MI;
    for (MI = (*SI)->begin(); MI != (*SI)->end(); ++MI) {

      // fish for phi-instructions
      if (!MI->isPHI()) continue;

      unsigned origIndex = getPHISourceRegIndex(*MI, origMBB);
      unsigned cloneIndex = getPHISourceRegIndex(*MI, cloneMBB);
      assert(!cloneIndex && "PHI-entry for the clone found!");

      // look for the entry for the original reg-value in the map
      unsigned reg = MI->getOperand(origIndex).getReg();
      DenseMap<unsigned, unsigned>::iterator V = VRMap.find(reg);
      if (V != VRMap.end()) reg = V->second;

      // now add a new operand for the (cloned) predecessor
      MI->addOperand(MachineOperand::CreateReg(reg, false));
      MI->addOperand(MachineOperand::CreateMBB(cloneMBB));

      DEBUG(
        dbgs() << "  added PHI-operand (reg): ";
        dbgs() << MI->getOperand(origIndex) << '\n';
        dbgs() << "  added PHI-operand (MBB): ";
        dbgs() << cloneMBB->getName() << '\n');
    }

    DEBUG(dbgs() << "Processing successor finished\n");
  }

  // when cloning a fallthrough basic block, there is a big probability, that
  // the terminator instructions are faulty, since we are going to insert the
  // clone at the very end of the function. Therefore correct this. NOTE, this
  // implicitly assumes the cloneMBB branch structure to be analyzable
  if (cloneMBB->succ_size()) cloneMBB->updateTerminator();
}

//----------------------------------------------------------------------------

MachineBasicBlock *
SuperblockFormation::cloneMachineBasicBlock(MachineBasicBlock *MBB,
                                          DenseMap<unsigned, unsigned> &VRMap)
{
  assert(MBB && "Bad machine basic block to copy from!");

  MachineFunction &MF = *(MBB->getParent());
  MachineRegisterInfo *MRI = &MF.getRegInfo();
  MachineBasicBlock *cloneMBB = MF.CreateMachineBasicBlock();

  // insert the clone instantly, this may help to avoid pretty strange surp-
  // rises such as inability to attach cloned instructions to the block due
  // to the operand use/def failures, etc
  MF.insert(MF.end(), cloneMBB);

  // when creating a machine basic block without specifying a parent, note,
  // that no successor-information is created. Therefore, patch in now. We
  // simply copy all succs from the original block and worry about changing
  // later. This is not the most efficient approach, but it simplifies the
  // implementation considerably
  MachineBasicBlock::succ_iterator SI;
  for (SI = MBB->succ_begin(); SI != MBB->succ_end(); ++SI)
    cloneMBB->addSuccessor(*SI);

  // TODO, FIXME, i am not sure yet, but the same will most probably apply to
  // the reg-values live into the original block as well, therefore, i patch
  // them into the clone to be sure for now
  MachineBasicBlock::livein_iterator LI;
  for (LI = MBB->livein_begin(); LI != MBB->livein_end(); ++LI)
    cloneMBB->addLiveIn(*LI);

  // now iterate over the entire list of available machine instructions in
  // the original basic block, rewrite defs to use the new virtual registers
  // and clone the instructions into the clone-block
  MachineBasicBlock::iterator MI;
  for (MI = MBB->begin(); MI != MBB->end(); ++MI) {
    MachineInstr *newMI = TII->duplicate(MI, MF);

    for (unsigned I = 0; I < newMI->getNumOperands(); ++I) {
      MachineOperand &MO = newMI->getOperand(I);
      if (!MO.isReg()) continue;

      // check for register definitions. We need to assign new virtual regs
      // to them in order to maintail the SSA properties
      const unsigned oldReg = MO.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(oldReg)) continue;

      if (MO.isDef()) {
        // rewrite defs to define new virtual registers now
        const TargetRegisterClass *RC = MRI->getRegClass(oldReg);
        const unsigned newReg = MRI->createVirtualRegister(RC);
        MO.setReg(newReg);

        // save the def-replacement entry about registers
        VRMap.insert(std::make_pair(oldReg, newReg));

        // if a value/reg is live out of the block, then, by cloning this bb
        // we pretty sure will destroy the SSA-properties, therefore we have
        // to add the new definition to the list of availale values for the
        // old def, so the update can correct this later
        if (isDefLiveOut(*MBB, oldReg)) {
          DEBUG(dbgs() << "  register " << MO << " is liveOut\n");
          addSSAUpdateEntry(oldReg, newReg, cloneMBB);
        }
      }
      else {
        // if not a def, check whether the reg has been defined earlier.
        // If yes, use the rewritten entry instead of the old one
        DenseMap<unsigned, unsigned>::iterator V = VRMap.find(oldReg);
        if (V != VRMap.end()) MO.setReg(V->second);
      }
    }

    cloneMBB->push_back(newMI);
  }

  return cloneMBB;
}

//----------------------------------------------------------------------------

void SuperblockFormation::updateSSA(MachineFunction &MF) {

  // NOTE, that this eventually will generate new phi's for blocks which pre-
  // viously had only one pred but now gained an additional (cloned) pred. If
  // there are no phi-instructions but simple copies, new phi's will be gene-
  // rated by the updater automatically

  SmallVector<MachineInstr*, 16> NewPHIs;
  MachineSSAUpdater SSAUpdater(MF, &NewPHIs);
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // check any of the rewritten virtual registers we have collected so far,
  // look for associations with new virtual regs/MBB pairs (i.e. available
  // values in the corresponding machine basic blocks) 
  for (unsigned I = 0; I < SSAUpdateVirtRegs.size(); ++I) {

    // throw in the original value definition
    const unsigned virtReg = SSAUpdateVirtRegs[I];
    SSAUpdater.Initialize(virtReg);

    MachineInstr *defMI = MRI.getVRegDef(virtReg);
    MachineBasicBlock *defMBB = 0;

    if (defMI) {
      defMBB = defMI->getParent();
      SSAUpdater.AddAvailableValue(defMBB, virtReg);
    }

    // throw in any of available values we have collected for the original
    // register so far for affected machine basic blocks
    DenseMap<unsigned, ValueVectorTy>::iterator VI =
      SSAUpdateVals.find(virtReg);

    if (VI != SSAUpdateVals.end())
      for (unsigned K = 0; K < VI->second.size(); ++K) {
        MachineBasicBlock *srcMBB = VI->second[K].first;
        unsigned srcReg = VI->second[K].second;

        SSAUpdater.AddAvailableValue(srcMBB, srcReg);
      }

    // now get the uses for machine basic blocks other than the original and
    // finally, let the machine-ssa-updater do the tough updating job for us
    MachineRegisterInfo::use_iterator UI = MRI.use_begin(virtReg);

    while (UI != MRI.use_end()) {
      MachineOperand &useMO = UI.getOperand();
      MachineInstr *useMI = &*UI;
      ++UI;
      
      if (useMI->getParent() == defMBB) continue;
      SSAUpdater.RewriteUse(useMO);
    }
  }
}

//----------------------------------------------------------------------------

void SuperblockFormation::eliminateSideEntries(const MBBListTy &SB) {

  /// first of all we need to find the position of the first side-entry, i.e.
  /// identify the longest tail we are going to create. Since we are not yet
  /// going to include created tails in new superblocks, we can use one and
  /// the same tail for every encountered side-entry
  assert(SB.size() > 1 && "Can not create tails for trivial superblocks!");

  MBBListTy::const_iterator tailBegin = SB.begin();

  // look for the first basic block that has more than 1 predecessor. FIXME,
  // this implicitly assumes, that there are no more than one edge from the
  // layout predecessor. Also NOTE, that we explicitly skip the trace-head,
  // since side-entry into the head-block are allowed by definition
  while (++tailBegin != SB.end()) if ((*tailBegin)->pred_size() > 1) break;

  if (tailBegin == SB.end()) return;

  // now, create an empty tail from the first side-entry position to the end
  // of the trace. Since we do not duplicate the entire superblock, we select
  // 'tailBegin' to point to the first basic block which is to be duplicated,
  // in the tail, 'tracePred' is the basic block which preceeds 'tailBegin'
  // in the list, and 'lastClone' is the last clone we have produced so far
  MachineFunction *MF = (*tailBegin)->getParent();

  // basic block preceeding the tail in the superblock
  MBBListTy::const_iterator TBI = tailBegin;
  MachineBasicBlock *tracePred = *(--TBI);
  MachineBasicBlock *lastClone = 0;

  // now, from the position of the first side-entry into the superblock (TBI)
  // to the end of this superblock, create clones and adjust preds and succs
  while (tailBegin != SB.end()) {

    DenseMap<unsigned, unsigned> VRegMap;

    // copy all machine instructions from the original machine block to the
    // the cloned machine basic block. Create new virtual registers for defs
    // and rewrite sources to use them
    MachineBasicBlock *origMBB = *tailBegin;
    MachineBasicBlock *cloneMBB = cloneMachineBasicBlock(origMBB, VRegMap);

    // now integrate the clone into the cfg properly, (i.e. without breaking
    // the semantics of course). This includes updating predecessor/successor
    // edges and phis, as well as correcting the destroyed (due to cloning)
    // SSA form
    updatePredInfo(origMBB, cloneMBB, tracePred, lastClone, VRegMap);
    updateSuccInfo(origMBB, cloneMBB, VRegMap);
    updateSSA(*MF);

    SSAUpdateVirtRegs.clear();
    SSAUpdateVals.clear();

    DEBUG(dbgs() << "Finished original MBB: " << *origMBB << "\n";);
    DEBUG(dbgs() << "Finished cloned MBB: " << *cloneMBB << "\n";);

    tracePred = origMBB;
    lastClone = cloneMBB;
    ++tailBegin;

    // update statistics
    ++NumDuplicatedBlocks;
  }
}

//----------------------------------------------------------------------------

void SuperblockFormation::processTrace(const MachineProfilePathBlockList &PP,
                                       const unsigned count)
{
  MachineProfilePathBlockList::const_iterator BI = PP.begin();
  MachineProfilePathBlockList::const_iterator BE = PP.end();

  /// Now we inspect the trace block-wise and check for each block whether it
  /// violates the superblock-constraints. If it does not, it is attached to
  /// the current superblock. But If it does, this basic block is skipped and
  /// a new superblock is created. This way the processing of one one trace
  /// may result in multiple superblocks being created

  while (BI != BE) {

    MBBListTy SB;

    // exclude bad blocks from superblock inclusion
    while ((BI != BE) && !isAttachableTo(*BI, SB)) ++BI;

    // include good blocks in a superblock, continue
    // until a bad block is encountered in the trace
    while ((BI != BE) && isAttachableTo(*BI, SB)) {
      processedBlocks.insert(*BI);
      SB.push_back(*BI);
      ++BI;
    }

    /// eventually a superblock is done, we additionally check the size of it.
    /// If it only consists of one basic-block, we do not mark it as processed
    /// yet and hope it to become a part of a bigger superblock later. However
    /// this can be done, if the basic block does not violate constraints for
    /// the superblock construction (such as having side-effect instructions)

    if (SB.size() > 1) {
      eliminateSideEntries(SB);
      MachineSuperBlock *superblock = new MachineSuperBlock(SB, count);
      superBlocks.insert(superblock);

      // check again and emit the content for the debug
      DEBUG(superblock->verify(); superblock->print());
      ++NumSuperBlocks;
    }
    else if (SB.size() == 1) {
      if (isDuplicable(**SB.begin()))
        processedBlocks.erase(*SB.begin());
    }
  }
}

//----------------------------------------------------------------------------

bool SuperblockFormation::runOnMachineFunction(MachineFunction &MF) {

  MachinePathProfileBuilder *builder =
    getAnalysisIfAvailable<MachinePathProfileBuilder>();

  if (!builder) return false;

  // since the builder makes use of multiple inheritance we convert it into
  // an analysis object to obtain the path profile info for machine blocks

  MachinePathProfileInfo *MPI = (MachinePathProfileInfo*)
    builder->getAdjustedAnalysisPointer(&MachinePathProfileInfo::ID);

  if (!MPI || MPI->isEmpty()) return false;

  DEBUG(dbgs() << "Run 'SuperblockFormation' pass for '"
               << MF.getFunction()->getNameStr() << "'\n");

  MLI = &getAnalysis<MachineLoopInfo>();
  MDT = &getAnalysis<MachineDominatorTree>();
  TII = MF.getTarget().getInstrInfo();

  clearSuperblockSet();
  processedBlocks.clear();

  NumSuperBlocks = 0;
  NumDuplicatedBlocks = 0;

  /// now process all constructed paths of machine basic blocks in descending
  /// order (with respect to the execution frequency of the machine bb-path)

  MachinePathProfileInfo::reverse_iterator I;
  for (I = MPI->rbegin(); I != MPI->rend(); ++I) {

    const unsigned count = I->first;
    const MachineProfilePathBlockList &blocks =
      I->second.getPathBlocks();

    if (blocks.size() && count > ExecThresh)
      processTrace(blocks, count);
  }

  NumSuperBlocksStat += NumSuperBlocks;
  NumDuplicatedBlocksStat += NumDuplicatedBlocks;

  if (NumDuplicatedBlocks) {
    MDT->runOnMachineFunction(MF);
    MLI->runOnMachineFunction(MF);
    return true;
  } else return false;
}

