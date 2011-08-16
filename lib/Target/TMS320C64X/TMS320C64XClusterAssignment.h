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
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/ADT/IndexedMap.h"

namespace llvm {
  class MachineBasicBlock;
  class MachineInstr;
  class TMS320C64XInstrInfo;

class TMS320C64XClusterAssignment : public MachineFunctionPass {

  TargetMachine &TM;
  const TMS320C64XInstrInfo *TII;
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

  typedef IndexedMap<const TargetRegisterClass*, VirtReg2IndexFunctor> VirtMap_t;
  void verifyUses(MachineFunction &MF, VirtMap_t &VirtMap);
  /*
  void verifyUses(MachineFunction &MF, MachineInstr *MI, const VirtMap_t &VirtMap,
                  bool fixConflicts = false);
                  */
  void fixUseRC(MachineFunction &MF, MachineInstr *MI, MachineOperand &MO,
                const TargetRegisterClass *RC);
};
}
