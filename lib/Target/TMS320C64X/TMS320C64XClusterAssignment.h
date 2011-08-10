//===-- TMS320C64XClusterAssignment.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Cluster assignment for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class TMS320C64XClusterAssignment : public MachineFunctionPass {

  TargetMachine &TM;

public:
  static char ID;

  TMS320C64XClusterAssignment(TargetMachine &tm)
    : MachineFunctionPass(ID), TM(tm) {}

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const char *getPassName() const {
    return "C64x+ cluster assignment";
  }

  bool runOnMachineFunction(MachineFunction &Fn) { return false; }

};
}
