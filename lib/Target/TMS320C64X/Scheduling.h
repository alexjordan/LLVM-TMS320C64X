//===-- TMS320C64X/Scheduling.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Base DAG scheduler for TMS320C64X
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_SCHEDULING_H
#define LLVM_TARGET_TMS320C64X_SCHEDULING_H

#include "llvm/CodeGen/ScheduleDAGInstrs.h"

namespace llvm {

namespace TMS320C64X {
  class SchedulerBase : public ScheduleDAGInstrs {
  public:
    SchedulerBase(MachineFunction &MF,
                  const MachineLoopInfo &MLI,
                  const MachineDominatorTree &MDT)
      : ScheduleDAGInstrs(MF, MLI, MDT) {}

    virtual void BuildSchedGraph(AliasAnalysis *AA);
  };
}
}
#endif
