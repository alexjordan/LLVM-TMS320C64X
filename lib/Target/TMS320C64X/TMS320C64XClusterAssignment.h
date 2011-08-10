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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Target/TargetInstrInfo.h"

namespace llvm {
  class MachineBasicBlock;
  class MachineInstr;

class TMS320C64XClusterAssignment : public MachineFunctionPass {

  TargetMachine &TM;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;

  typedef DenseMap<MachineInstr*, int> assignment_t;
  assignment_t Assigned;

public:
  static char ID;

  TMS320C64XClusterAssignment(TargetMachine &tm);

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const char *getPassName() const {
    return "C64x+ cluster assignment";
  }

  bool runOnMachineFunction(MachineFunction &Fn);


protected:
  typedef SmallSetVector<int,8> res_set_t;

  // assigns MI to res
  void assign(MachineInstr *MI, int res);

  // concrete assignment algorithms override these
  virtual void assignBasicBlock(MachineBasicBlock *MBB) = 0;

  // helpers
  void analyzeInstr(MachineInstr *MI, res_set_t &set) const;

};
}
