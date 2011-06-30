//===-- TMS320C64X/ClusterAssignment.h --------------------------*- C++ -*-===//
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

#ifndef LLVM_TARGET_TMS320C64X_CLUSTERASSIGNMENT_H
#define LLVM_TARGET_TMS320C64X_CLUSTERASSIGNMENT_H

#include "TMS320C64XTargetMachine.h"
#include "DAGHelper.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/IndexedMap.h"

namespace llvm {
  class SDNode;
  class SelectionDAG;
  class TargetInstrInfo;

namespace TMS320C64X {
  // abstract base for clustering algorithms
  class ClusteringHeuristic {
  public:
    virtual void run() = 0;
    virtual int getSide(const SDNode *N) const = 0;
    virtual int getUnit(const SDNode *N) const = 0;

    // for instruction replacement
    void apply(SelectionDAG *dag);
    virtual ~ClusteringHeuristic() {}
  protected:
    void reselect(SDNode *N, SelectionDAG *dag);
  };

  typedef SmallSetVector<int,8> res_set_t;
  typedef SmallVector<int,8> cycles_t;
  typedef std::pair<int,int> res_cycle_t;
  typedef SmallVector<int,8> resources_t;
  typedef std::pair<resources_t,cycles_t> op_list_t;

  class ClusterBug : public ClusteringHeuristic
                   , public DAGDebug::DebugsDAG {
    SelectionDAG *Dag;
    DAGHelper DagH;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    SDNode *EntryNode;
    std::set<SDNode*> Roots;
    SmallVector<int,128> Assigned;
    SmallVector<int,128> Cycle;
    DenseMap<const SDNode*,unsigned> Node2Id;
    IndexedMap<unsigned> ArgMap;

    // XXX could be SmallBitVector
    typedef SmallVector<BitVector,128> busycycles_t;
    busycycles_t Busy;

    static res_cycle_t unassigned;

    void likely(const SDNode *N, const op_list_t &ops,
        const res_set_t &dest, cycles_t &cycles);
    res_set_t likelyCopyToReg(const SDNode *N);
    res_set_t likelyCopyFromReg(const SDNode *N);
    res_set_t subtractBusy(const SDNode *N, const res_set_t &s);
    //res_set_t subtractUnsupported(const SDNode *N, const res_set_t &s);
    void initSupported(const SDNode *N, res_set_t &set);
    res_set_t restrictToSide(const res_set_t &s, int side);
    void bookCycle(const SDNode *N, const res_cycle_t &rc);
    // takes node and set of destinations, returns the assigned side
    res_cycle_t assign(const SDNode *N, const res_set_t &dest);

    bool isPredicable(const SDNode *N) const;
    bool filterOperand(const SDNode*, const SDValue&, unsigned);

  public:
    ClusterBug(SelectionDAG *dag, const TargetMachine &tm);
    void run();
    virtual int getSide(const SDNode *N) const;
    virtual int getUnit(const SDNode *N) const;
  };
}
}

#endif
