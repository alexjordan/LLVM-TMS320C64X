//===-- llvm/CodeGen/OptimizedPHIs.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Simple optimization/folding of PHI nodes
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_OPTIMIZEPHIS_H
#define LLVM_CODEGEN_OPTIMIZEPHIS_H

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/ADT/SmallPtrSet.h"

//----------------------------------------------------------------------------

namespace llvm {

class MachineInstr;
class MachineBasicBlock;
class MachineFunction;
class AnalysisUsage;
class TargetInstrInfo;

//----------------------------------------------------------------------------

class OptimizePHIs : public MachineFunctionPass {

    const TargetInstrInfo *TII;

    typedef SmallPtrSet<MachineInstr*, 16> InstrSet;
    typedef SmallPtrSetIterator<MachineInstr*> InstrSetIterator;

  public:

    static char ID; // Pass identification

    OptimizePHIs();

    virtual bool runOnMachineFunction(MachineFunction &MF);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

    static bool OptimizeBB(MachineBasicBlock &MBB);

    static bool IsSingleValuePHICycle(MachineInstr *MI,
                                      unsigned &SingleValReg,
                                      InstrSet &PHIsInCycle);

    static bool IsDeadPHICycle(MachineInstr *MI,
                               InstrSet &PHIsInCycle);
};

} // llvm namespace

#endif
