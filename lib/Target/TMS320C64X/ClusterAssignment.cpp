#include "ClusterAssignment.h"
#include "DAGHelper.h"
#include "TMS320C64XRegisterInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;
using namespace TMS320C64X;
using namespace llvm::DAGDebug;

namespace llvm {
namespace TMS320C64X {
  enum Sides {
    ASide = 0,
    BSide = 1
  };

  enum Resources {
    NUM_FUS = 4,
    NUM_SIDES = 2
  };

  // returns side or -1 if both are in set
  int sideOfRes(const res_set_t &set) {
    if (set.size() == 0)
      return -1;
    res_set_t::const_iterator I = set.begin(), E = set.end();
    int s = (*I++ & 0x1);
    for (;I != E; ++I)
      if (s != (*I & 0x1))
        return -1;
    return s;
  }

  res_cycle_t select(const res_set_t &r, const cycles_t &c) {
    if (r.size() == 0)
      return std::make_pair(0, 0);
    assert(r.size() == c.size());
    int minr = r[0];
    int minc = c[0];
    for (unsigned i = 1; i < c.size(); ++i)
      if (c[i] < minc) {
        minc = c[i];
        minr = r[i];
      }

    return std::make_pair(minr, minc);
  }

  template< class T >
  void addAllFUs(T& t, int side) {
    for (int i = 0; i < NUM_FUS; ++i)
      t.insert((i << 1) | (side & 0x1));
  }

  std::string res2Char(int r) {
    assert(r >= 0);
    static std::string a[] = { "L1", "S1", "M1", "D1" };
    static std::string b[] = { "L2", "S2", "M2", "D2" };
    std::string (&c)[4] = ((r & 0x1) == ASide) ? a : b;
    return c[r >> 1];
  }

  char side2Char(int s) {
    if (s == ASide)
      return 'A';
    assert(s == BSide);
    return 'B';
  }

  struct DbgSet {
    const res_set_t &s;
    DbgSet(const res_set_t &s) : s(s) {}
  };

  raw_ostream &operator<<(raw_ostream &os, const DbgSet &ds) {
    const res_set_t s = ds.s;
    os << "(";
    res_set_t::const_iterator I = s.begin(), E = s.end();
    if (I != E) {
      os << res2Char(*I++);
      for (;I != E; ++I)
        os << "," << res2Char(*I);
    }
    os << ")";
    return os;
  }

  struct DbgCycles {
    const cycles_t &s;
    DbgCycles(const cycles_t &s) : s(s) {}
  };

  raw_ostream &operator<<(raw_ostream &os, const DbgCycles &ds) {
    const cycles_t s = ds.s;
    os << "(";
    cycles_t::const_iterator I = s.begin(), E = s.end();
    if (I != E) {
      os << (*I++);
      for (;I != E; ++I)
        os << "," << (*I);
    }
    os << ")";
    return os;
  }

  void printRC(raw_ostream &os, const res_cycle_t &rc, char sep = '@') {
    if (rc.first < 0)
      os << "n/a";
    else
      os << res2Char(rc.first) << sep << rc.second;
  }

  raw_ostream &operator<<(raw_ostream &os, const res_cycle_t &rc) {
    printRC(os, rc);
    return os;
  }

  struct DbgRC {
    const op_list_t &l;
    DbgRC(const op_list_t &l) : l(l) {}
  };

  raw_ostream &operator<<(raw_ostream &os, const DbgRC &rc) {
    const resources_t r = rc.l.first;
    const cycles_t c = rc.l.second;
    assert(c.size() == r.size());
    os << "(";
    res_set_t::const_iterator RI = r.begin(), RE = r.end();
    cycles_t::const_iterator CI = c.begin();
    if (RI != RE) {
      printRC(os, std::make_pair(*RI++, *CI++), '>');
      for (;RI != RE; ++RI, ++CI) {
        os << ",";
        printRC(os, std::make_pair(*RI, *CI), '>');
      }

    }
    os << ")";
    return os;
  }

  // conveniently add the pair to the pair of lists
  void operator<<(op_list_t &list, const res_cycle_t &rc) {
    list.first.push_back(rc.first);
    list.second.push_back(rc.second);
  }
}
}


res_cycle_t TMS320C64X::ClusterBug::unassigned = std::make_pair(-1, -1);

TMS320C64X::ClusterBug::ClusterBug(SelectionDAG *dag,
                                   const TargetMachine &tm)
  : DebugsDAG(dag)
  , Dag(dag)
  , DagH(dag)
  , TII(tm.getInstrInfo())
  , TRI(tm.getRegisterInfo())
  , Assigned(dag->AssignTopologicalOrder(), -1)
  , Cycle(Assigned.size(), -1)
  , Busy(dag->allnodes_size() * 4, BitVector(8, false)) {
  DagH.analyze();

  // map nodes to their IDs once
  // (IDs change when we morph the nodes during reselection. this sucks)
  for (SelectionDAG::allnodes_const_iterator NI = Dag->allnodes_begin();
       NI != Dag->allnodes_end(); ++NI)
    Node2Id.insert(std::make_pair(NI.operator->(), NI->getNodeId()));
}

void TMS320C64X::ClusterBug::run() {
  const MachineRegisterInfo &MRI = Dag->getMachineFunction().getRegInfo();

// NKIM llvm-2.9 change
//  ArgMap.grow(MRI.getLastVirtReg());
  ArgMap.resize(MRI.getNumVirtRegs());

  for (MachineRegisterInfo::livein_iterator LI = MRI.livein_begin(),
       LE = MRI.livein_end(); LI != LE; ++LI) {
    errs() << "v" << LI->second << " -> arg-" << TRI->getName(LI->first) << "\n";
    ArgMap[LI->second] = LI->first;
  }


  EntryNode = Dag->getEntryNode().getNode();
  SDNode *root = Dag->getRoot().getNode();
  if (root->isMachineOpcode() && root->getMachineOpcode() == ret) {
    errs() << "skipping ret node\n";
    root = root->getOperand(2).getNode();
  }
  Roots.insert(root);

  // we may discover more root nodes by following chains
  while (Roots.size()) {
    errs() << "root nodes left: " << Roots.size() << "\n";
    SDNode *N = *Roots.begin();
    Roots.erase(N);
    // start with an empty set (ie. all units allowed)
    assign(N, res_set_t());
  }

}

int ClusterBug::getSide(const SDNode *N) const {
  assert(Node2Id.find(N) != Node2Id.end() &&
      N->getNodeId() == (int) Node2Id.lookup(N));
  return Assigned[N->getNodeId()] & 0x1;
}
int ClusterBug::getUnit(const SDNode *N) const {
  assert(Node2Id.find(N) != Node2Id.end() &&
      N->getNodeId() == (int) Node2Id.lookup(N));
  return Assigned[N->getNodeId()] >> 1;
}

res_cycle_t ClusterBug::assign(const SDNode *N, const res_set_t &dest) {
  if (N == EntryNode) {
    errs() << "reached entry\n";
    return unassigned;
  }

  assert((unsigned) N->getNodeId() < Assigned.size());
  if (Assigned[N->getNodeId()] >= 0) {
    errs() << "node[" << N->getNodeId() << "] already assigned\n";
    return std::make_pair(Assigned[N->getNodeId()], Cycle[N->getNodeId()]);
  }

  errs() << DbgNode(N, Dag) << "assigning... (" << N->getNumOperands()
    << " operand(s))\n";

  // static FU restriction (based on instruction)
  res_set_t likelyFUs;
  initSupported(N, likelyFUs);

  cycles_t likelyCycles(likelyFUs.size(), 0);

  // recurse on operands
  op_list_t opAssigned;
  for (unsigned i = 0, opNum = N->getNumOperands(); i < opNum; ++i) {
    const SDValue &op = N->getOperand(i);

    // we don't want to assign predicate or basic blocks
    if (filterOperand(N, op, i))
      continue;

    // update the likely set based on opRes while recursing
    likely(N, opAssigned, likelyFUs, likelyCycles);
    opAssigned << assign(op.getNode(), likelyFUs);
  }

  // update likely with the now fixed operands
  likely(N, opAssigned, likelyFUs, likelyCycles);

  errs() << DbgNode(N, Dag) << "op FUs " << DbgRC(opAssigned)
         << ", dest FUs " << DbgSet(dest)
         << ", likely FUs " << DbgSet(likelyFUs)
         << ", likely cycles " << DbgCycles(likelyCycles) << "\n";

  // assign
  res_cycle_t selected = select(likelyFUs, likelyCycles);
  Assigned[N->getNodeId()] = selected.first;
  Cycle[N->getNodeId()] = selected.second;

  // mark FU busy at cycle
  bookCycle(N, selected);

  errs() << DbgNode(N, Dag) << "assigned to " << selected << "\n";

  // return the completion cycle
  return std::make_pair(selected.first, selected.second + DagH.getLatency(N));
}

void ClusterBug::likely(const SDNode *N, const op_list_t &ops,
    const res_set_t &fus, cycles_t &cycles) {
  cycles_t::const_iterator maxIt =
    std::max_element(ops.second.begin(), ops.second.end());

  int start;
  if (maxIt == ops.second.end() || *maxIt < 0)
    start = DagH.getDepth(N);
  else
    start = *maxIt;


  assert(fus.size() == cycles.size());
  for (unsigned i = 0; i < fus.size(); ++ i) {
    int cycle = start;
    while (Busy[cycle].test(fus[i])) {
      errs() << print(N) << res2Char(fus[i]) << " busy @ " << cycle << "\n";
      cycle++;
    }
    cycles[i] = cycle;
  }

  //int side = sideOfRes(ops);
  // if ops favor a side, restrict dest to it
  //if (side >= 0)
  //  return restrictToSide(result, side);

}

res_set_t ClusterBug::likelyCopyToReg(const SDNode *N) {
  res_set_t l;
  unsigned DestReg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
  int DestSide = BRegsRegClass.contains(DestReg)?BSide:ASide;

  // mv is supported by all FUs
  addAllFUs(l, DestSide);

  errs() << DbgNode(N, Dag) << "restricted to " << DbgSet(l) << "\n";
  return l;
}

res_set_t ClusterBug::likelyCopyFromReg(const SDNode *N) {
  res_set_t l;
  unsigned SourceReg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
  if (!TargetRegisterInfo::isPhysicalRegister(SourceReg)) {
    // is it an argument?
    SourceReg = ArgMap[SourceReg];
    if (!SourceReg)
      return l; // no argument, copy from another vreg
  }

  int side = BRegsRegClass.contains(SourceReg)?BSide:ASide;

  addAllFUs(l, side);

  errs() << DbgNode(N, Dag) << "(arg " << TRI->getName(SourceReg) << ") "
    << "restricted to " << DbgSet(l) << "\n";
  return l;
}

bool ClusterBug::isPredicable(const SDNode *N) const {
  if (N->isMachineOpcode())
    return TII->get(N->getMachineOpcode()).isPredicable();
  return false;
}

res_set_t ClusterBug::subtractBusy(const SDNode *N, const res_set_t &s) {
  res_set_t result;
  for (res_set_t::iterator I = s.begin(), E = s.end(); I != E; ++I) {
    unsigned cycle = DagH.getDepth(N);
    if (Busy[cycle].test(*I))
      errs() << print(N) << res2Char(*I) << " busy @ " << cycle << "\n";
    else
      result.insert(*I);
  }
  return result;
}

void ClusterBug::initSupported(const SDNode *N, res_set_t &set) {
  switch (N->getOpcode()) {
  case ISD::CopyToReg:
    set = likelyCopyToReg(N);
    return;
  case ISD::CopyFromReg:
    set = likelyCopyFromReg(N);
    return;
  }

  if (!N->isMachineOpcode())
    return;

  unsigned flags = TII->get(N->getMachineOpcode()).TSFlags;
  unsigned us = flags & TMS320C64XII::unit_support_mask;
  set.clear();

  // special case 1: all units are supported by instruction
  if (us == 15) {
    // return the set empty
    return;
  }

  // special case 2: instruction is fixed
  if (us == 0) {
    unsigned fu = GET_UNIT(flags) << 1;
    fu |= IS_BSIDE(flags) ? 1 : 0;
    errs() << N->getOperationName(Dag) << " fixed to " << res2Char(fu)
           <<  "\n";
    set.insert(fu);
    return;
  }

  // from highest to lowest, the unit support bits are: L S M D
  for (int i = 0; i < NUM_FUS; ++i) {
    if ((us >> i) & 0x1) {
      set.insert(i << 1);
      set.insert((i << 1) + 1);
      errs() << "(" << res2Char(i << 1) << "," << res2Char((i << 1) +1)
        << ") supported by " << N->getOperationName(Dag) << "\n";
    }
  }
}
#if 0
res_set_t ClusterBug::subtractUnsupported(const SDNode *N, const res_set_t &s) {
  unsigned flags = TII->get(N->getMachineOpcode()).TSFlags;
  unsigned us = flags & TMS320C64XII::unit_support_mask;

  // special case 1: all units are supported by instruction
  if (us == 15)
    return s;

  // special case 2: instruction is fixed
  if (us == 0) {
    unsigned fu = GET_UNIT(flags) << 1;
    fu |= GET_SIDE(flags);
    errs() << N->getOperationName(Dag) << " fixed to " << res2Char(fu)
           <<  "\n";
    res_set_t result;
    result.insert(fu);
    return result;
  }

  res_set_t src, result;
  // special case 3: input set is unrestricted, add all possible units
  if (s.empty()) {
    addAllFUs(src, ASide);
    addAllFUs(src, BSide);
  }
  else
    src = s;

  for (res_set_t::iterator I = src.begin(), E = src.end(); I != E; ++I) {
    if ((us >> (*I >> 1)) & 1)
      result.insert(*I);
    else
      errs() << res2Char(*I) << " not supported by "
             << N->getOperationName(Dag) << "\n";
  }
  return result;
}
#endif

res_set_t ClusterBug::restrictToSide(const res_set_t &s, int side) {
  if (sideOfRes(s) >= 0) {
    if (side != sideOfRes(s))
      errs() << "already restricted to other side\n";
    return s;
  }

  res_set_t result;
  for (res_set_t::iterator I = s.begin(), E = s.end(); I != E; ++I) {
    if ((*I & 0x1) == side)
      result.insert(*I);
  }
  errs() << "restricted to side " << side2Char(side) << "\n";
  return result;
}

void ClusterBug::bookCycle(const SDNode *N, const res_cycle_t &rc) {
  // a pseudo-op in general does not occupy a cycle
  if (!N->isMachineOpcode()) {
    switch (N->getOpcode()) {
      // pseudo ops that become move instructions, do however
      case ISD::CopyToReg:
      case ISD::CopyFromReg:
        break;
      default:
        return;
    }
  }

  Busy[rc.second].set(rc.first);
  errs() << print(N) << "books resource " << res2Char(rc.first)
    << " @ " << rc.second << "\n";
}

bool ClusterBug::filterOperand(const SDNode *N, const SDValue &opval,
                               unsigned opIndex) {

  // these are all considered uninteresting for assignment
  switch (opval->getOpcode()) {
    case ISD::BasicBlock:
    case ISD::TargetConstant:
    case ISD::Register: // XXX what about predicate registers?
      return true;
  }

  if (N->isMachineOpcode()) {
    const TargetInstrDesc &tid = TII->get(N->getMachineOpcode());

    // also skip (hidden) chain operand but assign the chain later on
    if (opval.getValueType() == MVT::Other) {
      Roots.insert(opval.getNode());
      errs() << DbgNode(opval.getNode(), Dag) << "added to roots\n";
      return true;
    }
    // just skip any flags
    else if (opval.getValueType() == MVT::Glue) return true;

// NKIM change for llvm-2.9
//    else if (opval.getValueType() == MVT::Flag)
//      return true;

    // calls have a variable number of arguments, but no predicates.
    // it's okay to accept their operands, chain and flag are handled above
    if (N->getMachineOpcode() == TMS320C64X::callp_p)
      return false;

    assert(opIndex < tid.NumOperands);

    // skip predicate operands
    //if (tid.OpInfo[opIndex].isPredicate())
    //  return true;
  }

  // add any chained operands to roots
  if (opval.getValueType() == MVT::Other) {
    Roots.insert(opval.getNode());
    errs() << DbgNode(opval.getNode(), Dag) << "added to roots\n";
    return true;
  }

  return false;
}

// XXX this needs to move somewhere else
void ClusteringHeuristic::apply(SelectionDAG *dag) {
  SelectionDAG::allnodes_iterator ISelPosition
    = next(SelectionDAG::allnodes_iterator(dag->getRoot().getNode()));

  std::vector<bool> done(dag->allnodes_size(), false);
  SDNode *N = --ISelPosition;
  while (ISelPosition != dag->allnodes_begin()) {
    if (!done[N->getNodeId()]) {
      //errs() << "reselecting " << N->getNodeId() << " @ "<< N << "\n";
      reselect(N, dag);
      done[N->getNodeId()] = true;
      ISelPosition = next(SelectionDAG::allnodes_iterator(dag->getRoot().getNode()));
    }
    N = --ISelPosition;
  }
}

void ClusteringHeuristic::reselect(SDNode *N, SelectionDAG *dag)
{
  const TMS320C64XTargetMachine &TM =
    static_cast<const TMS320C64XTargetMachine &>
      (dag->getMachineFunction().getTarget());
  const TMS320C64XInstrInfo *TII = TM.getInstrInfo();

  if (!N->isMachineOpcode())
    return;

  int oldOpc = N->getMachineOpcode();
  int newOpc = TII->getOpcodeForSide(oldOpc, getSide(N));
  if (newOpc < 0) {
    errs() << N->getOperationName(dag) << " not mapped\n";
    return;
  }

  SmallVector<EVT, 4> VTs;
  for (unsigned i = 0, e = N->getNumValues(); i != e; ++i)
    VTs.push_back(N->getValueType(i));

  int unit = getUnit(N);
  if (unit < 0) {
    errs() << DbgNode(N, dag) << "not assigned\n";
    assert(false);
  }

  SmallVector<SDValue, 4> Ops;
  // copy operands and insert FU/form operand
  bool inserted = false;
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    SDValue Op = N->getOperand(i);

// NKIM, changed for llvm-2.9
//    if (!inserted && (Op.getValueType() == MVT::Other
//          || Op.getValueType() == MVT::Flag)) {
    if (!inserted && (Op.getValueType() == MVT::Other
    || Op.getValueType() == MVT::Glue))
    {
      assert(i >= 4); // expected after 2 src ops and 2 pred ops
      Ops.push_back(dag->getTargetConstant(
        TII->encodeInOperand(unit, 0), MVT::i32));
      inserted = true; // insert only once
    }
    Ops.push_back(Op);
  }

  // if it has not been inserted, append at end
  if (!inserted)
    Ops.push_back(dag->getTargetConstant(
      TII->encodeInOperand(unit, 0), MVT::i32));


  SDVTList VTList = dag->getVTList(&VTs[0], VTs.size());

// NKIM, changed for llvm-2.9
//  DebugLoc dl = DebugLoc::getUnknownLoc();
  DebugLoc dl;

  // XXX Morphing does a setMemRefs(0, 0), so we restore it below.
  MachineSDNode *MN = cast<MachineSDNode>(N);
  MachineSDNode::mmo_iterator mmo_begin = MN->memoperands_begin();
  MachineSDNode::mmo_iterator mmo_end = MN->memoperands_end();

  SDNode *morphed = dag->MorphNodeTo(N, ~newOpc, VTList, &Ops[0], Ops.size());

  // this is expecpted to be an in-place operation (if not, we're screwed)
  assert(N == morphed);

  MN->setMemRefs(mmo_begin, mmo_end);

  errs() << TII->get(oldOpc).getName() << " -> " << N->getOperationName(dag)
         << "\n";
}
