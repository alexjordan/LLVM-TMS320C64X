//===- MachineProfileInfoLoaderPass.cpp - LLVM Pass to load profile info --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a concrete implementation of profiling information that
// loads the information from a profile dump file.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-profile-analysis"
#include "llvm/Pass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineProfileAnalysis.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallSet.h"
#include <set>

using namespace llvm;

//-----------------------------------------------------------------------------

namespace {

class MachineProfileLoader : public MachineFunctionPass,
                             public MachineProfileAnalysis {

  public:

    static char ID;

    // default object ctor
    MachineProfileLoader()
    : MachineFunctionPass(ID) {
       initializeMachineProfileLoaderPass(*PassRegistry::getPassRegistry());
    }

    // main hook for the "loading" pass. Since we data is actually already
    // loaded (this job is done by corresponding objects within MachinePro-
    // fileAnalysis) we only need to "remap" the IR-structures on machine-
    // basic blocks/functions and paths here
    virtual bool runOnMachineFunction(MachineFunction &MF);

    // return a pointer to the proper object, since we make use of multiple
    // inheritance (MachineFunctionPass and/or MachineProfileAnalysis)
    void * getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &MachineProfileAnalysis::ID)
        return (MachineProfileAnalysis*)this;
      else return this;
    }

    // the pass itself is not destructive, however, since we supply the data
    // by external pointers, we actually also provide a way to clobber any
    // data we want to...
    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    const char *getPassName() const {
      return "Pass for extracting machine profile information";
    }

    // following method is provided for debugging only, it prints the map of
    // all machine basic block paths identified/constructed from the PI
    void emitMachineBlockPaths(MachineFunction &MF) const;

    // following method is provided for debugging only, it prints the content
    // of a basic block path as delivered by the path-profie-info loader pass
    void emitBasicBlockPath(ProfilePath &PP) const;

  private:

    // this stuff is used internally only. These methods ensure the "consis-
    // tency" of the profile-data, resp. applicability of the data onto the
    // current machine cfg. We do not yet patch particular edges and provide
    // only some simple path-reconstruction functionality

    // for cases when a single IR basic block is split into a number of MBBs
    // we can eventually extend the trace by looking for machine blocks which
    // refer to the same IR BB as the tail of the trace. Thats what this func
    // does. It returns a MBB that can be attached to the trace, or 0 if none
    // can be found (which is a regular case for 1:1 relationships)
    MachineBasicBlock *getExtension(MachineBasicBlock &MBB) const;

    // here a couple of checks is performed whether the specified machine bb
    // can be appended/attached to the MBB-path currently being reconstructed
    bool
    isAttachable(MachineProfilePathBlockList&, const MachineBasicBlock&) const;

    // this method inspects the entire function looking for the first machine
    // basic block which corresponds to the specified IR basic block. If it
    // can be found, the MBB is returned, otherwise 0 is returned
    MachineBasicBlock *getMBB(MachineFunction &MF, BasicBlock &BB) const;

    // this method processes one single profile-path of IR basic blocks and
    // tries to extract one (or many) machine basic block paths out of it
    void processTrace(MachineFunction &MF, ProfilePath &PP);

    // for now and only, for any other heavy hacking stuff
    ProfileInfo *getRawProfileInfo() const { return EPI; }
    PathProfileInfo *getRawPathProfileInfo() const { return PPI; }
};

}

// HACK, the loaded intermediate profile data
ProfileInfo *MachineProfileAnalysis::EPI = 0;
PathProfileInfo *MachineProfileAnalysis::PPI = 0;

//----------------------------------------------------------------------------

char MachineProfileAnalysis::ID = 0;
char MachineProfileLoader::ID = 0;

INITIALIZE_ANALYSIS_GROUP(MachineProfileAnalysis,
  "Machine profile information", MachineProfileLoader)

INITIALIZE_AG_PASS(MachineProfileLoader, MachineProfileAnalysis,
  "machine-profile-loader",
  "Load machine profile information from file", false, true, true)

FunctionPass *llvm::createMachineProfileLoaderPass() {
  return new MachineProfileLoader();
}

//----------------------------------------------------------------------------

double
MachineProfileAnalysis::getExecutionCount(MachineBasicBlock *MBB) const {
  assert(MBB && "Invalid machine block specified for the query!");
  const BasicBlock *BB = MBB->getBasicBlock();
  if (BB && EPI) return EPI->getExecutionCount(BB);
  else return .0;
}

//----------------------------------------------------------------------------

double
MachineProfileAnalysis::getExecutionCount(MachineFunction *MF) const {
  assert(MF && "Invalid machine function specified for the query!");
  const Function *F = MF->getFunction();
  if (F && EPI) return EPI->getExecutionCount(F);
  else return .0;
}

//----------------------------------------------------------------------------

double
MachineProfileAnalysis::getEdgeWeight(MachineProfileInfo::Edge E) const {
  if (!EPI) return .0;

  // check whether we are dealing with a real edge
  const MachineBasicBlock *MBB1 = E.first;
  const MachineBasicBlock *MBB2 = E.second;

  assert(MBB1 && MBB2 && "Invalid edge specified for the query!");
  if (!MBB1->isSuccessor(MBB2)) return .0;

  const BasicBlock *BB1 = E.first->getBasicBlock();
  const BasicBlock *BB2 = E.second->getBasicBlock();
  if (!BB1 || !BB2) return .0;

  for (succ_const_iterator I = succ_begin(BB1); I != succ_end(BB1); ++I) {
    if (*I == BB2) return EPI->getEdgeWeight(std::make_pair(BB1, BB2));
  }

  return .0;
}

//----------------------------------------------------------------------------

double MachineProfileAnalysis::getEdgeWeight(MachineBasicBlock *MBB1,
                                             MachineBasicBlock *MBB2) const
{
  return getEdgeWeight(std::make_pair(MBB1, MBB2));
}

//----------------------------------------------------------------------------

void MachineProfileLoader::emitBasicBlockPath(ProfilePath &PP) const {
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

void MachineProfileLoader::emitMachineBlockPaths(MachineFunction &MF) const {
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
MachineProfileLoader::getMBB(MachineFunction &MF, BasicBlock &BB) const {

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

bool MachineProfileLoader::isAttachable(MachineProfilePathBlockList &MP,
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
MachineProfileLoader::getExtension(MachineBasicBlock &MBB) const {

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

    // lost blocks ?
    if (!SBB) continue;

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

void MachineProfileLoader::processTrace(MachineFunction &MF, ProfilePath &PP) {

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

      std::set<MachineBasicBlock*> processedExtensions;

      // also check, whether we are dealing with a sequence of machine bbs,
      // i.e. a case when one IR basic block is split into multiple machine
      // basic blocks, and extend if possible. We can only extend the trace,
      // if the splitting is sequential and not parallel
      while (1) {
        processedExtensions.insert(MBB);

        // check whether the tail of the trace can be extended, i.e. there is
        // a (unique) MBB-successor that corresponds to the same basic block
        if (MachineBasicBlock *extensionMBB = getExtension(*MBB)) {
          if (!processedExtensions.count(extensionMBB)) {
            processedExtensions.insert(extensionMBB);
            BlockList.push_back(extensionMBB);
            MBB = extensionMBB;
          } else break;
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

//----------------------------------------------------------------------------

bool MachineProfileLoader::runOnMachineFunction(MachineFunction &MF) {

  // Does not work this way yet, we do not aquire the desired path profile
  // information via getAnalysis but specify the information explicitly
  // during the construction of this pass. This works, but is clearly not
  // very clean, since actually ignoring the pass ordering/dependencies of
  // the LLVM-pass handling framework
  //  PPI = &getAnalysis<PathProfileInfo>();

  DEBUG(dbgs() << "Running MachineProfileLoader on '"
               << MF.getFunction()->getName() << "'\n");

  MachineProfilePaths.clear();

  if (PPI) {
    Function *F = const_cast<Function*>(MF.getFunction());
    assert(F && "Invalid function for the machine code!");

    PPI->setCurrentFunction(F);

    // we can't yet process functions for whose no paths have been recorded,
    // i.e. which have not been executed/profiled, since we do not implement
    // a path calculation itself but rather a reconstruction/verification of
    // an already created/given set of paths
    if (PPI->pathBegin() != PPI->pathEnd()) {
      // now check all profiled paths as delivered by the path-profile-loader
      for (ProfilePathIterator I = PPI->pathBegin(); I != PPI->pathEnd(); ++I)
        processTrace(MF, *(I->second));

      // show reconstructed paths
      DEBUG(emitMachineBlockPaths(MF));
    }
    DEBUG(dbgs() << "Path profile information processed\n");
  }
// else errs() << "Warning: no path profile information found!\n";
  return false;
}

