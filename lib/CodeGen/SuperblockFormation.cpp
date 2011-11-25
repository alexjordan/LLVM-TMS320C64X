//===-- llvm/CodeGen/SuperblockFormation.cpp --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Perform a formation of superblocks (represented as a list of machine blocks)
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "superblock-formation"
#include "llvm/CodeGen/SuperblockFormation.h"
#include "llvm/CodeGen/TailReplication.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
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

MachineSuperBlockMapTy
SuperblockFormation::superBlocks = MachineSuperBlockMapTy();

char SuperblockFormation::ID = 0;

//----------------------------------------------------------------------------

INITIALIZE_PASS_BEGIN(SuperblockFormation, "superblock-formation",
                "Profile Guided Superblock Formation", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_AG_DEPENDENCY(MachineProfileAnalysis)
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

SuperblockFormation::~SuperblockFormation() { clearSuperblockMap(); }

//----------------------------------------------------------------------------

void SuperblockFormation::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineProfileAnalysis>();
  AU.addRequired<MachineDominatorTree>();
  AU.addRequired<MachineLoopInfo>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

//----------------------------------------------------------------------------

void SuperblockFormation::clearSuperblockMap() {
  while (superBlocks.size()) {
    MachineSuperBlock *MSB = superBlocks.begin()->second;
    superBlocks.erase(superBlocks.begin());
    delete MSB;
  }
}

//----------------------------------------------------------------------------

const MachineSuperBlockMapTy &SuperblockFormation::getSuperblocks() {
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
  MachineBasicBlock *enteringBlock = *tailBegin;
  tailBegin++;

  while (tailBegin != SB.end()) {
    if ((*tailBegin)->pred_size() > 1) break;

    enteringBlock = *tailBegin;
    tailBegin++;
  }

  if (tailBegin == SB.end()) return;

  // now create a tail for the replicator. Start at the tailBegin position we
  // have calculated above and continue to the very end of the superblock...
  MBBListTy tail;
  for (MBBListTy::const_iterator TI = tailBegin; TI != SB.end(); ++TI)
    tail.push_back(*TI);

  assert(enteringBlock && "Invalid block entering the tail detected!");
  assert(tail.size() && "Can not duplicate an empty block tail!");

  // use a tail replicator now for removing any side entries into the super-
  // block. For the replicator we only need to supply the block falling into
  // the tail, and the tail itself
  TailReplication tailReplicator(TII);
  tailReplicator.duplicateTail(*enteringBlock, tail);
  NumDuplicatedBlocks += tail.size();
}

//----------------------------------------------------------------------------

void SuperblockFormation::processTrace(const MachineProfilePathBlockList &PP,
                                       const unsigned count)
{
  MachineProfilePathBlockList::const_iterator BI = PP.begin();
  MachineProfilePathBlockList::const_iterator BE = PP.end();

  /// Now we inspect the trace block-wise and check for each block whether it
  /// violates the superblock-constraints. If it does not, it is attached to
  /// the current superblock. But if it does, this basic block is skipped and
  /// a new superblock is created. This way the processing of one trace may
  /// result in multiple superblocks being created

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

      MachineSuperBlock *superblock =
        new MachineSuperBlock(*(SB.front()->getParent()), *SB.front(), SB);

      superBlocks.insert(std::make_pair(count, superblock));

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

  clearSuperblockMap();
  processedBlocks.clear();

  NumSuperBlocks = 0;
  NumDuplicatedBlocks = 0;

  MachineProfileAnalysis *builder =
    getAnalysisIfAvailable<MachineProfileAnalysis>();

  if (!builder || builder->pathsEmpty()) return false;

  DEBUG(dbgs() << "Run 'SuperblockFormation' pass for '"
               << MF.getFunction()->getNameStr() << "'\n");

  MLI = &getAnalysis<MachineLoopInfo>();
  MDT = &getAnalysis<MachineDominatorTree>();
  TII = MF.getTarget().getInstrInfo();

  /// now process all constructed paths of machine basic blocks in descending
  /// order (with respect to the execution frequency of the machine bb-path)

  MachineProfileAnalysis::reverse_iterator I;
  for (I = builder->rbegin(); I != builder->rend(); ++I) {

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

