//===-- TMS320C64X/DAGHelper.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// XXX this code should probably be moved to cluster assignment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_DAGHELPER_H
#define LLVM_TARGET_TMS320C64X_DAGHELPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
  class SelectionDAG;
  class SDNode;

  namespace DAGDebug {
    // for debug output
    struct DbgNode {
      const SDNode *N;
      const SelectionDAG *Dag;
      DbgNode(const SDNode *N, const SelectionDAG *dag) 
      : N(N), Dag(dag) {}
    };

    class DebugsDAG {
      const SelectionDAG *DebugDag;
    protected:
      DebugsDAG(const SelectionDAG *dag) : DebugDag(dag) {}
      DbgNode print(const SDNode *N) {
        return DbgNode(N, DebugDag);
      }
    };
  }

  class DAGHelper : public DAGDebug::DebugsDAG {
    const SelectionDAG *Dag;
    typedef DenseMap<const SDNode*, unsigned> DepthMap_t;
    DepthMap_t DepthMap;

    unsigned getDepthFromAbove(const SDNode *N);
  public:
    DAGHelper(const SelectionDAG *dag)
    : DebugsDAG(dag), Dag(dag) {}

    void analyze();
    unsigned getDepth(const SDNode *N) const;
    unsigned getLatency(const SDNode *N);
  };

  llvm::raw_ostream &operator<<(llvm::raw_ostream&, const DAGDebug::DbgNode&);
}

#endif
