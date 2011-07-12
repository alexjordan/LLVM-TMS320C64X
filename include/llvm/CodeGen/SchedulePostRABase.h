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
  public:
    SchedulePostRABase(MachineFunction &MF,
                         const MachineLoopInfo &MLI,
                         const MachineDominatorTree &MDT)
      : ScheduleDAGInstrs(MF, MLI, MDT) {}

    virtual void Observe(MachineInstr *MI, unsigned Count) = 0;
    virtual void FixupKills(MachineBasicBlock *MBB) = 0;

    // return the number of cycles for last scheduled region
    virtual unsigned getCycles() const = 0;
  };
}

#endif
