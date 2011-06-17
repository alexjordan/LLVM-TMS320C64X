#define DEBUG_TYPE "clustering"

#include "DAGHelper.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"


using namespace llvm;
using namespace DAGDebug;

void DAGHelper::analyze() {
  // set depth of the entry (leaf)
  DepthMap[Dag->getEntryNode().getNode()] = 0;
  // let's make love and getDepthFromAbove
  getDepthFromAbove(Dag->getRoot().getNode());
}

unsigned DAGHelper::getDepthFromAbove(const SDNode *N) {
  DepthMap_t::iterator it = DepthMap.find(N);
  if (it != DepthMap.end())
    return it->second;

  unsigned max = 0;
  for (SDNode::op_iterator I = N->op_begin(), E = N->op_end(); I != E; ++I) {
    SDNode *op = I->getNode();
    max = std::max(max, getDepthFromAbove(op) + getLatency(op));
  }

  DEBUG(dbgs() << print(N) << "depth = " << max << "\n");
  DepthMap[N] = max;
  return max;
}

unsigned DAGHelper::getDepth(const SDNode *N) const {
  DenseMap<const SDNode*, unsigned>::const_iterator it = DepthMap.find(N);
  assert(it != DepthMap.end());
  return it->second;
}

unsigned DAGHelper::getLatency(const SDNode *N) {
  const TargetMachine &TM = Dag->getMachineFunction().getTarget();
  const TargetInstrInfo *TII = TM.getInstrInfo();

  switch (N->getOpcode()) {
  case ISD::Register:
    return 0;
  }

  // this looked good at first...
  //if (N->isMachineOpcode())
  //  return 1;
  //return 0;

  // ... falling back to unit latencies makes more sense though
  if (N->isMachineOpcode()) {
    int delay = GET_DELAY_SLOTS(TII->get(N->getMachineOpcode()).TSFlags);
    return delay + 1; // latency = delay slots + 1
  }

  return 1;
}

raw_ostream &llvm::operator<<(raw_ostream &os, const DbgNode &dn) {
  os << "node[" << dn.N->getNodeId() << "|"
     << dn.N->getOperationName(dn.Dag) << "]: ";
  return os;
}

