//===-- llvm/CodeGen/SuperblockFormation.h ----------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_SUPERBLOCKFORMATION_H
#define LLVM_CODEGEN_SUPERBLOCKFORMATION_H

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/CodeGen/MachineProfileAnalysis.h"
#include "llvm/ADT/DenseMap.h"

// stl stuff
#include <list>
#include <set>
#include <map>

//----------------------------------------------------------------------------

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineLoopInfo;
class MachineDominatorTree;
class AnalysisUsage;
class TargetInstrInfo;

//----------------------------------------------------------------------------

typedef MachineSingleEntryPathRegion MachineSuperBlock;
typedef std::multimap<unsigned, MachineSuperBlock*> MachineSuperBlockMapTy;

//----------------------------------------------------------------------------

class SuperblockFormation : public MachineFunctionPass {

    /// Data members

    MachineLoopInfo *MLI;
    MachineDominatorTree *MDT;
    const TargetInstrInfo *TII;

    /// pass statistics
    unsigned NumSuperBlocks;
    unsigned NumDuplicatedBlocks;

    /// this is a map we store all created/identified nontrivial superblocks
    /// in. We use the execution count of the trace as a key to the multimap
    static MachineSuperBlockMapTy superBlocks;

    /// this is a temporary container that helps keeing track of machine bbs
    /// that have been processed already and can be (or actually must not be)
    /// processed again
    std::set<const MachineBasicBlock*> processedBlocks;

    /// Methods

    /// this helper is required to drop all collected/created superblocks ni-
    /// cely without creeating memory-leaks
    void clearSuperblockMap();

    /// this method examines the specified profiled execution-path and tries
    /// to create superblocks. This includes checks for constraints and then
    /// performing a tail-duplication for each identified and created non-
    /// trivial superblock
    void processTrace(const MachineProfilePathBlockList &, const unsigned C);

    /// this method performs constraints checks during the initial phase of a
    /// superblock-construction. This includes checks for instructions which
    /// potentially may have side-effects, checks for cycles/backedges, etc.
    bool isAttachableTo(MachineBasicBlock *MB, MBBListTy &SB) const;

    /// when building superblocks we need to determine whether a basic block
    /// contains any instructions that may have side-effects (such as calls).
    /// These basic blocks are then skipped/ignored during construction
    bool isDuplicable(const MachineBasicBlock &BB) const;

    /// in order to eliminate any existing side entries into a superlock, we
    /// implement some kind of a tail-duplication. For the time being, this
    /// tail is created once and is shared by all side-entries into the SB
    void eliminateSideEntries(const MBBListTy &SB);

  public:

    static char ID;

    SuperblockFormation();
    ~SuperblockFormation();

    virtual bool runOnMachineFunction(MachineFunction &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

    /// get the entire map of created superblocks at once
    static const MachineSuperBlockMapTy &getSuperblocks();
};

} // llvm namespace

#endif
