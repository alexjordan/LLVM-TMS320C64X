//===-- llvm/CodeGen/TailReplication.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Perform a tail duplication/replication in order to remove side entriees from
// a given region.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "tail-replication"
#include "llvm/CodeGen/TailReplication.h"
#include "llvm/CodeGen/OptimizePHIs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//----------------------------------------------------------------------------

bool TailReplication::verifyTail(const MBBListTy &tail) const {
  if (!tail.size()) return false;

  MachineBasicBlock *prevMBB = tail.front();
  for (MBBListTy::const_iterator I = tail.begin(); I != tail.end(); ++I) {
    // no trivial cycles permitted for blocks
    if ((*I)->isSuccessor(*I)) return false;

    // skip the entry basic block
    if ((*I) == tail.front()) continue;

    // now verify the linked chain of machine basic blocks within the path
    if (!prevMBB->isSuccessor(*I)) return false;
    prevMBB = *I;
  }
  return true;
}

//----------------------------------------------------------------------------

unsigned
TailReplication::getPHISourceRegIndex(const MachineInstr &PHIInstr,
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

bool TailReplication::isDefLiveOut(const MachineBasicBlock &MBB,
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

void TailReplication::addSSAUpdateEntry(unsigned oldReg,
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

void TailReplication::updatePredInfo(MachineBasicBlock *origMBB,
                                     MachineBasicBlock *cloneMBB,
                                     MachineBasicBlock *tracePred,
                                     MachineBasicBlock *clonePred,
                                     DenseMap<unsigned, unsigned> &VRMap)
{
  assert(origMBB && cloneMBB && tracePred && "Bad MBB, can't update preds!");

  /// First of all, scan over the current machine basic block for the tail.
  /// Look for phi-instructions and change/update/remove the entry for each
  /// predecessor that presents a side-entry into the block (i.e. all preds
  /// except the one that preceeds in the path region). We need to do this,
  /// because all side-entries (i.e. preds) will later be directed to enter
  /// the clones instead of the origs

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
      if (!predIndex) continue;

      // entries for phi instructions must be pairs of reg/block values
      assert(MI->getOperand(predIndex + 1).isMBB() && "Bad MBB operand!");
      assert(MI->getOperand(predIndex).isReg() && "Bad register operand!");

      DEBUG(
        dbgs() << "  removed: " << MI->getOperand(predIndex + 1) << '\n';
        dbgs() << "  removed: " << MI->getOperand(predIndex) << '\n');

      MI->RemoveOperand(predIndex + 1); // remove entry for side-entry-mbb
      MI->RemoveOperand(predIndex);     // remove entry for side-entry-reg
    }
  }

  /// now we update all predecessors of the original machine block. This step
  /// effectively removes any side entries from the original basic block and
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
  }
}

//----------------------------------------------------------------------------

void TailReplication::updateSuccInfo(MachineBasicBlock *origMBB,
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
TailReplication::cloneMachineBasicBlock(MachineBasicBlock *MBB,
                                        DenseMap<unsigned, unsigned> &VRMap)
{
  assert(MBB && "Bad machine basic block to copy from!");

  MachineFunction &MF = *(MBB->getParent());
  MachineRegisterInfo *MRI = &MF.getRegInfo();

  const BasicBlock *BB = MBB->getBasicBlock();
  MachineBasicBlock *cloneMBB = MF.CreateMachineBasicBlock(BB);

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
      // to them in order to maintain the SSA properties
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
        // to add the new definition to the list of available values for the
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

void TailReplication::updateSSA(MachineFunction &MF) {

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

void TailReplication::duplicateTail(MachineBasicBlock &headMBB,
                                MachineBasicBlock &tailMBB)
{
  MBBListTy tailList;
  tailList.push_back(&tailMBB);
  duplicateTail(headMBB, tailList);
}

//----------------------------------------------------------------------------

void TailReplication::duplicateTail(MachineBasicBlock &head,
                                    const MBBListTy &tail)
{
  assert(verifyTail(tail) && "Bad tail specified for duplication!");
  MachineBasicBlock *tailEntry = tail.front();

  // now run some simple static checks to assure proper linkage
  assert(head.isSuccessor(tailEntry) && "Bad tail linkage!");
  assert(tailEntry->pred_size() > 1 && "No side entries into the tail!");

  // now, create an empty tail from the first side-entry position (i.e. tail-
  // entry) to the very end of the specified trace
  MachineFunction *MF = tailEntry->getParent();
  MBBListTy::const_iterator tailBegin = tail.begin();

  // this is the MBB preceeding the tail
  MachineBasicBlock *tracePred = &head;
  MachineBasicBlock *lastClone = 0;

  // now, from the position of the first side-entry into (i.e. tail-head TBI)
  // the tail to its end, we create clones and patch predecessors/successors
  while (tailBegin != tail.end()) {

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

    OptimizePHIs::OptimizeBB(*origMBB);
    OptimizePHIs::OptimizeBB(*cloneMBB);

    DEBUG(dbgs() << "Finished original MBB: " << *origMBB << "\n";);
    DEBUG(dbgs() << "Finished cloned MBB: " << *cloneMBB << "\n";);

    tracePred = origMBB;
    lastClone = cloneMBB;
    ++tailBegin;
  }
}

