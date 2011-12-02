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

  class MachineSingleEntryPathRegion;

namespace TMS320C64X {
  class SchedulerBase : public ScheduleDAGInstrs {
  public:
    SchedulerBase(MachineFunction &MF,
                  const MachineLoopInfo &MLI,
                  const MachineDominatorTree &MDT)
      : ScheduleDAGInstrs(MF, MLI, MDT) {}

    virtual void BuildSchedGraph(AliasAnalysis *AA);

    // experimental dag builder with generic instr iteration
    template <class ForwardIter>
    void BuildSchedGraph(ForwardIter first, ForwardIter last,
                                AliasAnalysis *AA);
    static bool hasSideEffects(const MachineInstr *MI);
  };

  class RegionScheduler : protected SchedulerBase {
    typedef MachineSingleEntryPathRegion MachineRegion;
    MachineSingleEntryPathRegion *MR;

  protected:
    bool isBeingScheduled(MachineBasicBlock *MBB) const;
    void preprocess(MachineSingleEntryPathRegion *MR) const;

  public:
    RegionScheduler(MachineFunction &MF,
                    const MachineLoopInfo &MLI,
                    const MachineDominatorTree &MDT)
      : SchedulerBase(MF, MLI, MDT), MR(0) {}

    virtual void BuildSchedGraph(AliasAnalysis *AA);
    // returns the entry  block of the region
    virtual MachineBasicBlock *EmitSchedule();
    void Run(MachineSingleEntryPathRegion *R);
  };
}
}
#endif
