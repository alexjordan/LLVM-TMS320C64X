//===-- TMS320C64X/ClusterDAG.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares cluster assignment helper for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_CLUSTERDAG_H
#define LLVM_TARGET_TMS320C64X_CLUSTERDAG_H

#include "TMS320C64XClusterAssignment.h"
#include "Scheduling.h"
#include "TMS320C64XHazardRecognizer.h"
#include "llvm/CodeGen/LatencyPriorityQueue.h"

namespace llvm {
  class SDNode;
  class SelectionDAG;
  class TargetInstrInfo;

namespace TMS320C64X {

  std::pair<int,int> countOperandSides(const MachineInstr *MI,
                                       MachineRegisterInfo &MRI);

  class ClusterDAG : public TMS320C64X::SchedulerBase {
  protected:
    ResourceAssignment *FUSched;
    const TargetRegisterInfo *TRI;
    AssignmentState *CAState;
    std::vector<SUnit*> CopySUs;

  public:
    ClusterDAG(MachineFunction &MF,
               const MachineLoopInfo &MLI,
               const MachineDominatorTree &MDT,
               AssignmentState *state)
      : SchedulerBase(MF, MLI, MDT)
      , FUSched(NULL)
      , TRI(MF.getTarget().getRegisterInfo())
      , CAState(state)
    {}

    virtual ~ClusterDAG();

    void setFunctionalUnitScheduler(ResourceAssignment *r) {
      FUSched = r;
    }

  };

  class ClusterPriority {
    SmallVector<int, 2> Clusters;
    int last;
  public:
    ClusterPriority() : last(-1) {}
    ClusterPriority(unsigned c) : last(-1) {
      Clusters.push_back(c);
    }
    ClusterPriority(unsigned higher, unsigned lower) : last(-1) {
      Clusters.push_back(lower);
      Clusters.push_back(higher);
    }

    bool hasMore() const { return Clusters.size(); }
    int getNext() { return last = Clusters.pop_back_val(); }
    int getLast() const { return last; }
  };

  class UAS : public ClusterDAG {
  public:
    enum PriorityFunction {
      None,
      Random,
      MWP
    };

  private:
    LatencyPriorityQueue AvailableQueue;
    std::vector<SUnit*> PendingQueue;

    PriorityFunction PF;
    unsigned RandState; // for randomized cluster priority

    void ScheduleTopDown();

    void ReleaseSucc(SUnit *SU, SDep *SuccEdge);
    void ReleaseSuccessors(SUnit *SU);
    void ScheduleNodeTopDown(SUnit *SU, unsigned CurCycle, unsigned side);

    bool rewriteAssignedCluster(SUnit *SU, unsigned side);
    void rewriteDefs(SUnit *SU);
    void rewriteUses(SUnit *SU);

    ClusterPriority getClusterPriority(SUnit *SU);
    ClusterPriority prioRandom(MachineInstr *MI);
    ClusterPriority prioMWP(MachineInstr *MI);

    // returns (assigned) side of an SUnit
    unsigned getAssignedSide(SUnit *SU);
    // returns side of a vreg (based on the assignment of its defining SUnit)
    unsigned getVRegSide(unsigned reg);
    // returns Xcc-copy-vreg of an existing vreg
    unsigned getVRegOnSide(unsigned reg, unsigned side);

    bool neverHazard(SUnit *SU);
    bool XccStall(SUnit *SU, unsigned side, unsigned CurCycle);
    bool needCopy(SUnit *SU, unsigned side, SmallVector<unsigned,2> &regs,
                  SmallVector<unsigned,2> &ops);
    unsigned insertCopy(unsigned reg, unsigned side);
    bool supportsXPath(SUnit *SU, unsigned opNum);
    const TargetRegisterClass *sideToRC(unsigned side) const;
  public:
    UAS(MachineFunction &MF, const MachineLoopInfo &MLI,
        const MachineDominatorTree &MDT, AssignmentState *state)
      : ClusterDAG(MF, MLI, MDT, state)
      , PF(MWP)
      , RandState(0)
    {}

    virtual void Schedule();

    void setPriority(PriorityFunction p) { PF = p; }

    // whether to use xpath implicitly (otherwise everything is copied)
    static const bool UseXPath = 1;
  };

  /// factory function
  ClusterDAG *createClusterDAG(AssignmentAlgorithm AA,
                               MachineFunction &MF,
                               const MachineLoopInfo &MLI,
                               const MachineDominatorTree &MDT,
                               AssignmentState *state);
}
}

#endif
