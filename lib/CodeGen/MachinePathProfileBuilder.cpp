//===-- llvm/CodeGen/MachineProfilePathBuilder.cpp -  MPP-Builder ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-profile-path-builder"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachinePathProfileBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

char MachinePathProfileInfo::ID = 0;
char MachinePathProfileBuilder::ID = 0;

//-----------------------------------------------------------------------------

INITIALIZE_PASS(MachinePathProfileBuilder, "machine-path-builder",
  "Pass for building machine block paths from the profile info", false, false)

//-----------------------------------------------------------------------------

void MachinePathProfileBuilder::setPathProfileInfo(ModulePass *PLP) {
  if (PLP) {
    PPI = (PathProfileInfo *)
      PLP->getAdjustedAnalysisPointer(&PathProfileInfo::ID);
  }
}

//-----------------------------------------------------------------------------

MachinePathProfileBuilder::MachinePathProfileBuilder()
: MachineFunctionPass(ID),
  PPI(0)
{
  initializeMachinePathProfileBuilderPass(*PassRegistry::getPassRegistry());
}

//-----------------------------------------------------------------------------

void *MachinePathProfileBuilder::getAdjustedAnalysisPointer(AnalysisID PI) {
  if (PI == &MachinePathProfileInfo::ID)
    return (MachinePathProfileInfo*)this;
  else return this;
}

//-----------------------------------------------------------------------------

void MachinePathProfileBuilder::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);

  // NOTE, we do depend on the PathProfileInfo analysis, but do not require
  // it. I did not yet find a way to aquire a PathProfileAnalysis via the
  // getAnalysis template which wouldnt be empty...Therefore, i use a work-
  // around for the time being and pass the info via the ctor explicitly...
  // Thats ugly, but it works for now, improve later, TODO
  // AU.addRequired<PathProfileInfo>();
}

//-----------------------------------------------------------------------------

const char *MachinePathProfileBuilder::getPassName() const {
  return "Pass for creating machine basic block execution paths";
}

//-----------------------------------------------------------------------------

void MachinePathProfileBuilder::emitBasicBlockPath(ProfilePath &PP) const {
  ProfilePathBlockVector *blocks = PP.getPathBlocks();
  assert(blocks && "Invalid basic-block-pointer for a profile-trace!");

  if (!blocks->size()) return;

  dbgs() << "basic block path [count " << PP.getCount() << "]:\n";

  ProfilePathBlockIterator BI;
  for (BI = blocks->begin(); BI != blocks->end(); ++BI) {
    if ((*BI)->hasName()) dbgs() << "\t\t" << (*BI)->getName() << '\n';
    else dbgs() << "\t\tunnamed-bb\n";
  }

  dbgs() << '\n';
}

//-----------------------------------------------------------------------------

void
MachinePathProfileBuilder::emitMachineBlockPaths(MachineFunction &MF) const {
  for (const_iterator
       I = MachineProfilePaths.begin(); I != MachineProfilePaths.end(); ++I)
  {
    dbgs() << "machine path [count " << I->first << "]:\n";
    const MachineProfilePathBlockList &MBBP = I->second.getPathBlocks();

    MachineProfilePathBlockList::const_iterator J;
    for (J = MBBP.begin(); J != MBBP.end(); ++J)
      dbgs() << "\t\t" << (*J)->getName() << '\n';

    dbgs() << '\n';
  }
}

//-----------------------------------------------------------------------------

MachineBasicBlock*
MachinePathProfileBuilder::getMBB(MachineFunction &MF, BasicBlock &BB) const {

  for (MachineFunction::iterator I = MF.begin(); I != MF.end(); ++I) {

    if (!(I->getBasicBlock())) continue;

    // for the time being consider named blocks only
    if (!(I->getBasicBlock()->hasName())) continue;

    // TODO, to keep things simple, we do not yet consider situations when a
    // basic block is split into multiple machine basic blocks and return the
    // first one encountered in the function-layout. Alternatively we could
    // return a (ordered by block number ?) set of machine basic blocks which
    // correspond to the specified IR block
    if (I->getBasicBlock() == &BB) return &*I;
  }

  return 0;
}

//-----------------------------------------------------------------------------

bool MachinePathProfileBuilder::isAttachable(MachineProfilePathBlockList &MP,
                                            const MachineBasicBlock &MBB) const
{
  // attachable to the empty path
  if (!MP.size()) return true;

  // also attachable to the path, if the specified machine basic block is a
  // successor of the last block stored in the machine path so far, i.e. the
  // trace is not going to be disrupted by insertion of the specified MBB,
  // and also corresponds to the same basic block
  if (MP.back()->isSuccessor(&MBB)) return true;

  // when a basic block just "extends" the tail of the current trace
  if (MP.back()->getBasicBlock() == MBB.getBasicBlock()) return true;

  return false;
}

//-----------------------------------------------------------------------------

MachineBasicBlock *
MachinePathProfileBuilder::getExtension(MachineBasicBlock &MBB) const {

  const BasicBlock *BB = MBB.getBasicBlock();
  assert(BB && "Invalid basic block for MBB!");

  /// now inspect all successors of the given machine basic block and return
  /// a machine basic block which we consider as extension, i.e. a successor
  /// that of the given MBB that refers to the same basic block

  unsigned numExtensions = 0;
  MachineBasicBlock *extensionMBB = 0;

  MachineBasicBlock::succ_iterator SI, SE;
  for (SI = MBB.succ_begin(), SE = MBB.succ_end(); SI != SE; ++SI) {

    const BasicBlock *SBB = (*SI)->getBasicBlock();
    assert(SBB && "Invalid basic block for MBB-successor!");

    // avoid trivial cycles, i.e. one block loops. TODO, however, i don't yet
    // check for non-trivial loops, i.e. loops spanning more than 1 machine bb
    // with all of them refering to the same bb...don't know probable this is
    if (*SI == &MBB) continue;

    // now check for any extension-candidates and count them
    if (SBB == BB) { extensionMBB = (*SI); ++numExtensions; }
  }

  // if there are more than 1 successor refering to the same basic block as
  // MBB, we actually can not decide which one to prefer. For simplicity we
  // indicate that there is no extension possible in such cases
  return numExtensions == 1 ? extensionMBB : 0;
}

//-----------------------------------------------------------------------------

void
MachinePathProfileBuilder::processTrace(MachineFunction &MF, ProfilePath &PP) {

  ProfilePathBlockVector *blocks = PP.getPathBlocks();
  assert(blocks && "Bad basic blocks for a profile-trace!");

  ProfilePathBlockIterator BI = blocks->begin();

  while (BI != blocks->end()) {

    MachineProfilePathBlockList BlockList;

    // in the most pleasant case there is a true 1:1 relation between a chain
    // of IR basic blocks and the corresponding MBB pendant. However, in most
    // of the cases we expect an IR basic block chain to be split into many
    // MBB-traces

    while (BI != blocks->end()) {

      // first of all inspect the entire function looking for a machine bb,
      // which corresponds to the basic block being currently processed. We
      // do not yet pay attention to cases when multiple machine basic blocks
      // refer to the same bb and simply return the first one which passes a
      // comparison by name
      MachineBasicBlock *MBB = getMBB(MF, **BI);

      // for the time being we require a machine BB to exist
      assert(MBB && "Can't find a MBB for a basic block!");

      if (!isAttachable(BlockList, *MBB)) break;

      // first of all we need to insert the found machine basic block which
      // corresponds to the IR basic block within the current profile path
      BlockList.push_back(MBB);

      // also check, whether we are dealing with a sequence of machine bbs,
      // i.e. a case when one IR basic block is split into multiple machine
      // basic blocks, and extend if possible. We can only extend the trace,
      // if the splitting is sequential and not parallel
      while (1) {

        // check whether the tail of the trace can be extended, i.e. there is
        // a (unique) MBB-successor that corresponds to the same basic block
        if (MachineBasicBlock *extensionMBB = getExtension(*MBB)) {
          BlockList.push_back(extensionMBB);
          MBB = extensionMBB;
        } else break;
      }

      ++BI;
    }

    if (BlockList.size() > 1) {
      // we don't number paths yet, and also do not make the count to be the
      // the part of the profile-path. Instead, we use the count as a key to
      // the multimap which does a sorting for us
      MachineProfilePath MPP(0, BlockList);
      MachineProfilePaths.insert(std::make_pair(PP.getCount(), MPP));
    }
  }
}

//-----------------------------------------------------------------------------

bool MachinePathProfileBuilder::runOnMachineFunction(MachineFunction &MF) {

  MachineProfilePaths.clear();

  // Does not work this way yet, we do not aquire the desired path profile
  // information via getAnalysis but specify the information explicitly
  // during the construction of this pass. This works, but is clearly not
  // very clean, since actually ignoring the pass ordering/dependencies of
  // the LLVM-pass handling framework
  //  PPI = &getAnalysis<PathProfileInfo>();

  if (!PPI) return false;

  Function *F = const_cast<Function*>(MF.getFunction());
  assert(F && "Invalid function for the machine code!");

  PPI->setCurrentFunction(F);

  DEBUG(dbgs() << "Running MachinePathProfileBuilder on: "
               << MF.getFunction()->getName() << '\n');

  // we can't yet process functions for whose no paths have been recorded,
  // i.e. which have not been executed/profiled, since we do not implement
  // a path calculation itself but rather a reconstruction/verification of
  // an already created/given set of paths
  if (PPI->pathBegin() == PPI->pathEnd()) return false; 

  // now check all profiled paths as delivered by the path-profile-loader
  for (ProfilePathIterator I = PPI->pathBegin(); I != PPI->pathEnd(); ++I)
    processTrace(MF, *(I->second));

  // show reconstructed paths
  DEBUG(emitMachineBlockPaths(MF));
  DEBUG(dbgs() << "MachinePathProfileBuilder finished.\n");
  return false;
}

