//===- MachineProfileAnalysis.h -------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file outlines the interface used by optimizers to load machine profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MACHINEPROFILEANALYSIS_H
#define LLVM_MACHINEPROFILEANALYSIS_H

#include "llvm/Analysis/PathProfileInfo.h"

//-----------------------------------------------------------------------------

namespace llvm {

class MachineProfileAnalysis {

  public:

    /// Types
    typedef MachineProfilePathMap::iterator iterator;
    typedef MachineProfilePathMap::const_iterator const_iterator;
    typedef MachineProfilePathMap::reverse_iterator reverse_iterator;

  protected:

    MachineProfilePathMap MachineProfilePaths;

  public:

    static char ID;

    // intermediate stuff, thats ugly but ok, that is best to be moved to the
    // machine profile load impl, since the estimating implementation can not
    // do anything with it, however, leave this monstrosity for now...
    static ProfileInfo *EPI;     // edges
    static PathProfileInfo *PPI; // paths

    MachineProfileAnalysis() {}

    /// Iterators

    iterator begin() { return MachineProfilePaths.begin(); }
    const_iterator begin() const { return MachineProfilePaths.begin(); }

    iterator end() { return MachineProfilePaths.end(); }
    const_iterator end() const { return MachineProfilePaths.end(); }

    reverse_iterator rbegin() { return MachineProfilePaths.rbegin(); }
    reverse_iterator rend() { return MachineProfilePaths.rend(); }

    /// Simple accessors (readonly)

    bool pathsEmpty() const { return !MachineProfilePaths.size(); }
    unsigned pathsSize() const { return MachineProfilePaths.size(); }

    // information queries, for now offer a restricted interface only
    virtual double getExecutionCount(const MachineBasicBlock *MBB);
    virtual double getExecutionCount(const MachineFunction *MF);
    virtual double getEdgeWeight(MachineProfileInfo::Edge E);
    virtual double getEdgeWeight(const MachineBasicBlock*,
                                 const MachineBasicBlock*);
};

} // end namespace llvm

#endif

