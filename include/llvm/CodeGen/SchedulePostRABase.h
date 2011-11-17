//===---- llvm/CodeGen/PostRASchedulerBase.h - Common Base Class-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares an abstract base for post RA schedulers
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEPOSTRA_H
#define LLVM_CODEGEN_SCHEDULEPOSTRA_H

#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"

namespace llvm {
  class MachineFunction;
  class MachineModuleInfo;
  class MachineRegisterInfo;
  class MachineInstr;

  class SchedulePostRABase : public ScheduleDAGInstrs {

    /// KillIndices - The index of the most recent kill (proceding bottom-up),
    /// or ~0u if the register is not live.
    std::vector<unsigned> KillIndices;

  public:
    SchedulePostRABase(MachineFunction &MF,
                         const MachineLoopInfo &MLI,
                         const MachineDominatorTree &MDT)
      : ScheduleDAGInstrs(MF, MLI, MDT)
      , KillIndices(TRI->getNumRegs())
    {}

    virtual void Observe(MachineInstr *MI, unsigned Count) = 0;

    /// FixupKills - Fix register kill flags that have been made
    /// invalid due to scheduling
    ///
    virtual void FixupKills(MachineBasicBlock *MBB);

    // return the number of cycles for last scheduled region
    virtual unsigned getCycles() const = 0;

  protected:
    // ToggleKillFlag - Toggle a register operand kill flag. Other
    // adjustments may be made to the instruction if necessary. Return
    // true if the operand has been deleted, false if not.
    bool ToggleKillFlag(MachineInstr *MI, MachineOperand &MO);

    void StartBlockForKills(MachineBasicBlock *BB);
  };
}

#endif
