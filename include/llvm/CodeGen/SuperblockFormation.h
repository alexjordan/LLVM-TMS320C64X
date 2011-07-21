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
#include "llvm/CodeGen/MachineSuperBlock.h"
#include "llvm/CodeGen/MachinePathProfileBuilder.h"
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

typedef std::set<MachineSuperBlock*> MachineSuperBlockSetTy;
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
    SmallVector<unsigned, 1024> SSAUpdateVirtRegs;

    /// this a set we store all created/identified non-trivial superblocks in.
    /// For the time being we offer a simple set only (for simplicity) but may
    /// provide a more sophisticated and adapted container later
    MachineSuperBlockSetTy superBlocks;

    /// this is a temporary container that helps keeing track of machine bbs
    /// that have been processed already and can be (or actually must not be)
    /// processed again
    std::set<const MachineBasicBlock*> processedBlocks;

    /// Methods

    /// this method examines the specified profiled execution-path and tries
    /// to create superblocks. This includes checks for constraints and then
    /// performing a tail-duplication for each identified and created non-
    /// trivial superblock
    void processTrace(const MachineProfilePathBlockList &, const unsigned C);

    /// determine whether the specified machine basic block has only one of
    /// its predecessors already being attached to the specified list
    bool hasOnlyOnePredInList(MachineBasicBlock *B, const MBBListTy &L) const;

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
    /// new virtual registers as done by copyMachineBlockContent, an update
    /// of the predecessors is required. All predecessors of origMBB (except
    /// one) will be changed to preceed cloneMBB now, thus eliminating side-
    /// entries. Only the block preceeding in the superblock will be ignored
    void updatePredInfo(MachineBasicBlock *origMBB,
                        MachineBasicBlock *cloneMBB,
                        MachineBasicBlock *traceOrigPred,
                        MachineBasicBlock *traceClonePred,
                        DenseMap<unsigned, unsigned> &VRMap);

    /// the same rules as for predecessors applies when it comes to handle/
    /// update the phi-instructions for the successors. Cloned blocks are
    /// updated to succeed each other instead of the original blocks, and the
    /// phi-instructions of the non-trace successors (i.e. side-exits) are
    /// now extented since now receiving new values from the clones
    void updateSuccInfo(MachineBasicBlock *origMBB,
                        MachineBasicBlock *cloneMBB,
                        MachineBasicBlock *traceOrigSucc,
                        MachineBasicBlock *traceCloneSucc,
                        DenseMap<unsigned, unsigned> &VRMap);

    /// since we have rewritten virtual register defs and their local uses,
    /// as well as patching the phi-nodes from the original and cloned bbs,
    /// we most probably have destroyed the SSA-form of the code, which we
    /// need to restore for the later passes
    void updateSSA(MachineFunction &MF);

    /// in order to eliminate any existing side entries into a superlock, we
    /// implement some kind of a tail-duplication. For the time being, this
    /// tail is created once and is used for all side-entries
    void eliminateSideEntries(const MBBListTy &SB);

    /// drop superblocks nicely
    void clearSuperblockSet();

  public:

    static char ID;

    SuperblockFormation();
    ~SuperblockFormation();

    virtual bool runOnMachineFunction(MachineFunction &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

    /// get the entire set of created superblocks at once
    const MachineSuperBlockSetTy &getSuperblocks() const;
};

} // llvm namespace

#endif
