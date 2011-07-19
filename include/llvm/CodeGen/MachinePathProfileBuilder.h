//===-- llvm/CodeGen/MachineProfilePathBuilder.h -  MPP-Builder -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPATHBUILDER_H
#define LLVM_CODEGEN_MACHINEPATHBUILDER_H

#include "llvm/Analysis/PathProfileInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "list"
#include "map"

namespace llvm {

class ProfilePath;
class PathProfileInfo;

//------------------------------------------------------------------------------

class MachinePathProfileBuilder : public MachineFunctionPass,
                                  public MachinePathProfileInfo
{
    /// Data members

    // profile data containing information about the paths of the program.
    // This data is essential for this pass, and is required to be existant.
    // NOTE, however, since the path-profile-analysis is currently implemented
    // for the intermed. representation only, and the loading pass is a module
    // pass, there are some restrictions, about how to use the analysis within
    // the llc-backend. Due to these restrictions we don't use the getAnalysis
    // template to aquire the PPI, but specify it explicitly instead
    PathProfileInfo *PPI;

    /// Methods

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

    // following method is provided for debugging only, it prints the content
    // of a basic block path as delivered by the path-profie-info loader pass
    void emitBasicBlockPath(ProfilePath &PP) const;

    // following method is provided for debugging only, it prints the map of
    // all machine basic block paths identified/constructed from the PI
    void emitMachineBlockPaths(MachineFunction &MF) const;

  public:

    static char ID;

    explicit MachinePathProfileBuilder();

    // NOTE, specify the path-profile-info explicitly instead of aquiring it
    // via the getAnalysis-template which does not seem to work yet. The pass
    // for loading the PPI is an IR-module-pass which has to be run before the
    // machine-module-info has been established. Specifying the pass/analysis
    // dependencies does not seem to work, therefore i provide a workaround
    // for the time being
    void setPathProfileInfo(ModulePass *loader);

    // analysis usage hook for the pass (require PPI only)
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

    // return a static name for the pass
    virtual const char *getPassName() const;

    // since we intend to implement a multiple inheritance
    virtual void *getAdjustedAnalysisPointer(AnalysisID PI);

    // execution method hook for the machine function pass
    virtual bool runOnMachineFunction(MachineFunction &MF);
};

} // llvm namespace

#endif
