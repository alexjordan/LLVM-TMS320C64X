//===-- llvm/CodeGen/TailReplication.h --------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_TAILREPLICATION_H
#define LLVM_CODEGEN_TAILREPLICATION_H

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/ADT/DenseMap.h"
#include <list>

//----------------------------------------------------------------------------

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class TargetInstrInfo;

//----------------------------------------------------------------------------

typedef std::pair<MachineBasicBlock*, MachineBasicBlock*> MBBPairTy;
typedef std::pair<MachineBasicBlock*, unsigned> MBBValPairTy;
typedef std::vector<MBBValPairTy> ValueVectorTy;
typedef std::list<MBBPairTy> MBBPairList;

//----------------------------------------------------------------------------

class TailReplication {

    /// Data members
    const TargetInstrInfo *TII;

    /// temp containers for the SSA-housekeeping
    DenseMap<unsigned, ValueVectorTy> SSAUpdateVals;
    SmallVector<unsigned, 256> SSAUpdateVirtRegs;

    /// this is a temporary container that helps keeing track of machine bbs
    /// that have been processed already and can be (or actually must not be)
    /// processed again
    std::set<const MachineBasicBlock*> processedBlocks;

    /// Methods

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

  public:

    TailReplication(const TargetInstrInfo *TIIparam)
    : TII(TIIparam) {}

    ~TailReplication() {}

    /// a handy helper method for finding the index of the register operand
    /// for the specified machine basic block (within the specified instr.)
    unsigned getPHISourceRegIndex(const MachineInstr &PHIInstr,
                                  MachineBasicBlock *sourceMBB) const;

    /// determine whether the specified register is live out of the block
    bool isDefLiveOut(const MachineBasicBlock &MBB, unsigned reg) const;

    /// in order to eliminate any existing side entries into a region, we
    /// implement some kind of a tail-duplication. For the time being, this
    /// tail is created once and is shared by all side-entries into the node
    void duplicateTail(MachineBasicBlock &head, const MBBListTy &tail);

    /// another duplication variant for convenience for two blocks only
    void duplicateTail(MachineBasicBlock &head, MachineBasicBlock &tail);

    /// a simple helper to ensure tail's properties, first of all a proper
    /// linkage between contained machine basic blocks
    bool verifyTail(const MBBListTy &tail) const;
};

} // llvm namespace

#endif
