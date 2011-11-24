//===---- llvm/CodeGen/ScheduleInstrsCommon.h -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares a helper class for machine instrutions schedulers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULE_INSTRS_COMMON_H
#define LLVM_CODEGEN_SCHEDULE_INSTRS_COMMON_H

#include "llvm/CodeGen/ScheduleDAGInstrs.h"

namespace llvm {
  class MachineFunction;
  class MachineModuleInfo;
  class MachineRegisterInfo;
  class MachineInstr;
  class TargetRegisterInfo;

  class ScheduleInstrsCommon {

    const TargetRegisterInfo *TRI;
    MachineRegisterInfo &MRI;
    MachineFunction &MF;

    /// KillIndices - The index of the most recent kill (proceding bottom-up),
    /// or ~0u if the register is not live.
    std::vector<unsigned> KillIndices;

  public:
    ScheduleInstrsCommon(MachineFunction &mf)
      : TRI(mf.getTarget().getRegisterInfo())
      , MRI(mf.getRegInfo())
      , MF(mf)
      , KillIndices(TRI->getNumRegs())
    {}

    virtual void Observe(MachineInstr *MI, unsigned Count) = 0;

    /// FixupKills - Fix register kill flags that have been made
    /// invalid due to scheduling
    ///
    virtual void FixupKills(MachineBasicBlock *MBB);

  protected:
    // ToggleKillFlag - Toggle a register operand kill flag. Other
    // adjustments may be made to the instruction if necessary. Return
    // true if the operand has been deleted, false if not.
    bool ToggleKillFlag(MachineInstr *MI, MachineOperand &MO);

    void StartBlockForKills(MachineBasicBlock *BB);
  };
}

#endif
