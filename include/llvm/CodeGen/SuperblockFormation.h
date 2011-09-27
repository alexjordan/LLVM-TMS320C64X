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
class MachineRegisterInfo;
class AnalysisUsage;
class TargetInstrInfo;

//----------------------------------------------------------------------------

typedef MachineSingleEntryPathRegion MachineSuperBlock;
typedef std::multimap<unsigned, MachineSuperBlock*> MachineSuperBlockMapTy;
typedef std::pair<MachineBasicBlock*, MachineBasicBlock*> MBBPairTy;
typedef std::pair<MachineBasicBlock*, unsigned> MBBValPairTy;
typedef std::vector<MBBValPairTy> ValueVectorTy;
typedef std::list<MBBPairTy> MBBPairList;

//----------------------------------------------------------------------------

class SuperblockFormation : public MachineFunctionPass {

    /// Data members

    MachineLoopInfo *MLI;
    MachineDominatorTree *MDT;
    const TargetInstrInfo *TII;

    /// pass statistics
    unsigned NumSuperBlocks;
    unsigned NumDuplicatedBlocks;

    /// temp containers for the SSA-housekeeping
    DenseMap<unsigned, ValueVectorTy> SSAUpdateVals;
    SmallVector<unsigned, 256> SSAUpdateVirtRegs;

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

    /// a handy helper method for finding the index of the register operand
    /// for the specified machine basic block (within the specified instr.)
    unsigned getPHISourceRegIndex(const MachineInstr &PHIInstr,
                                  MachineBasicBlock *sourceMBB) const;

    /// determine whether the specified register is live out of the block
    bool isDefLiveOut(const MachineBasicBlock &MBB, unsigned reg) const;

    /// this method records a pair of associated old/new registers to main-
    /// tain the SSA property of the pre-regAlloc machine code
    void addSSAUpdateEntry(unsigned oldR, unsigned newR, MachineBasicBlock*);

    /// this method copies instructions from origMBB to cloneMBB, creates new
    /// virtual registers for defs in the clone and rewrites sources to use
    /// them. All <old, new> defs register associations are stored in the map
    MachineBasicBlock *cloneMachineBasicBlock(MachineBasicBlock *origMBB,
                                     DenseMap<unsigned, unsigned> &VRMap);

    /// after having cloned a basic block, rewritten defs and sources to use
    /// new virtual registers as done by cloneMachineBasicBlock, an update
    /// of the predecessors is required. All predecessors of origMBB (except
    /// one) will be changed to preceed cloneMBB now, thus eliminating side-
    /// entries. Only the block preceeding in the superblock will be ignored
    void updatePredInfo(MachineBasicBlock *origMBB,
                        MachineBasicBlock *cloneMBB,
                        MachineBasicBlock *traceOrigPred,
                        MachineBasicBlock *traceClonePred,
                        DenseMap<unsigned, unsigned> &VRMap);

    /// for simplicity we copy all successors during block-cloning. However,
    /// now each successor of the original basic block will get an additional
    /// predecessor (clone). Therefore an update of successor's phi-instruc-
    /// tions is required
    void updateSuccInfo(MachineBasicBlock *origMBB,
                        MachineBasicBlock *cloneMBB,
                        DenseMap<unsigned, unsigned> &VRMap);

    /// since we have rewritten virtual register defs and their local uses,
    /// as well as patching the phi-nodes from the original and cloned bbs,
    /// we most probably have destroyed the SSA-form of the code, which we
    /// certainly need to restore for the later passes
    void updateSSA(MachineFunction &MF);

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
