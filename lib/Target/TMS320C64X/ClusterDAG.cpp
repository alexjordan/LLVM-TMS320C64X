//===-- TMS320C64X/ClusterDAG.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines cluster assignment helper for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cluster-assignment"
#include "ClusterDAG.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/Statistic.h"

#include "llvm/Support/Debug.h"
#undef DEBUG
#define DEBUG(x) x

using namespace llvm;
using namespace llvm::TMS320C64X;

STATISTIC(XccStalls, "Number of XPATH stalls avoided");
STATISTIC(XccInserted, "Number of cross-cluster copies inserted");

ClusterDAG::~ClusterDAG() {
  for (std::vector<SUnit*>::iterator I = CopySUs.begin(), E = CopySUs.end();
       I != E; ++I)
    delete *I;
}

/// ReleaseSucc - Decrement the NumPredsLeft count of a successor. Add it to
/// the PendingQueue if the count reaches zero. Also update its cycle bound.
void UAS::ReleaseSucc(SUnit *SU, SDep *SuccEdge) {
  SUnit *SuccSU = SuccEdge->getSUnit();

#ifndef NDEBUG
  if (SuccSU->NumPredsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    SuccSU->dump(this);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(0);
  }
#endif
  --SuccSU->NumPredsLeft;

  // Compute how many cycles it will be before this actually becomes
  // available.  This is the max of the start time of all predecessors plus
  // their latencies.
  SuccSU->setDepthToAtLeast(SU->getDepth() + SuccEdge->getLatency());

  // If all the node's predecessors are scheduled, this node is ready
  // to be scheduled. Ignore the special ExitSU node.
  if (SuccSU->NumPredsLeft == 0 && SuccSU != &ExitSU)
    PendingQueue.push_back(SuccSU);
}

/// ReleaseSuccessors - Call ReleaseSucc on each of SU's successors.
void UAS::ReleaseSuccessors(SUnit *SU) {
  for (SUnit::succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    ReleaseSucc(SU, &*I);
  }
}

/// ScheduleNodeTopDown - Add the node to the schedule. Decrement the pending
/// count of its successors. If a successor pending count is zero, add it to
/// the Available queue.
void UAS::ScheduleNodeTopDown(SUnit *SU, unsigned CurCycle, unsigned side) {
  DEBUG(dbgs() << "*** Scheduling [" << CurCycle << "]: ");
  DEBUG(SU->dump(this));
  // rewrite instruction if assigned to other cluster
  if (rewriteAssignedCluster(SU, side)) {
    MachineOperand &MO = SU->getInstr()->getOperand(0);
    if (MO.isReg() && MO.isDef()) {
      // need to change the register class
      MRI.setRegClass(MO.getReg(), sideToRC(side));
      CAState->addVChange(MO.getReg(), sideToRC(side));
    }
    DEBUG(dbgs() << "**** Cluster assignment: ");
    DEBUG(SU->dump(this));
  } else {
    // for some pseudo instructions (that are not assigned) we need to assign
    // the defined register
    rewriteDefs(SU);
  }

  rewriteUses(SU);

  /*
  MachineOperand &MO = SU->getInstr()->getOperand(0);
  if (MO.isReg() && MO.isDef() &&
      (MRI.getRegClass(MO.getReg()) == GPRegsRegisterClass)) {
    SU->dump(this);
    assert(false && "GPReg assignment");
  }
  */


  Sequence.push_back(SU);
  assert(CurCycle >= SU->getDepth() &&
         "Node scheduled above its depth!");
  SU->setDepthToAtLeast(CurCycle);

  ReleaseSuccessors(SU);
  SU->isScheduled = true;
  AvailableQueue.ScheduledNode(SU);
}


void UAS::Schedule() {
  BuildSchedGraph(NULL);

  DEBUG(dbgs() << "********** UAS Scheduling **********\n");
  DEBUG(for (unsigned su = 0, e = SUnits.size(); su != e; ++su)
          SUnits[su].dumpAll(this));

  AvailableQueue.initNodes(SUnits);
  ScheduleTopDown();
  AvailableQueue.releaseState();

  EmitSchedule();
}

void UAS::ScheduleTopDown() {
  unsigned CurCycle = 0;

  // Release any successors of the special Entry node.
  ReleaseSuccessors(&EntrySU);

  // Add all leaves to Available queue.
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    // It is available if it has no predecessors.
    bool available = SUnits[i].Preds.empty();
    if (available) {
      AvailableQueue.push(&SUnits[i]);
      SUnits[i].isAvailable = true;
    }
  }

  // When scheduling XCC, we make an implicit assignment decision for a later
  // cycle.
  std::map<SUnit*, ClusterPriority> preAssigned;
  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  std::vector<SUnit*> NotReady, WaitingForCopy;
  Sequence.reserve(SUnits.size());
  while (!AvailableQueue.empty() || !PendingQueue.empty()) {
    // Check to see if any of the pending instructions are ready to issue.  If
    // so, add them to the available queue.
    unsigned MinDepth = ~0u;
    for (unsigned i = 0, e = PendingQueue.size(); i != e; ++i) {
      if (PendingQueue[i]->getDepth() <= CurCycle) {
        AvailableQueue.push(PendingQueue[i]);
        PendingQueue[i]->isAvailable = true;
        PendingQueue[i] = PendingQueue.back();
        PendingQueue.pop_back();
        --i; --e;
      } else if (PendingQueue[i]->getDepth() < MinDepth)
        MinDepth = PendingQueue[i]->getDepth();
    }

    DEBUG(dbgs() << "\n*** Examining Available\n"; AvailableQueue.dump(this));

    SUnit *FoundSUnit = 0;
    ClusterPriority cp;

    while (!AvailableQueue.empty()) {
      SUnit *CurSUnit = AvailableQueue.pop();

      if (neverHazard(CurSUnit)) {
        FoundSUnit = CurSUnit;
        break;
      }

      std::map<SUnit*, ClusterPriority>::iterator found = preAssigned.find(CurSUnit);
      if (found != preAssigned.end())
        cp = found->second;
      else
        cp = getClusterPriority(CurSUnit);
      bool scheduled = false;
      bool copy_scheduled = false;
      bool xpath = false;
      while (scheduled == false && copy_scheduled == false && cp.hasMore()) {
        int c = cp.getNext();
        DEBUG(dbgs() << "**** Trying on cluster " << c << "\n");
        if (XccStall(CurSUnit, c, CurCycle))
          continue;

        SmallVector<unsigned, 2> regs, ops;
        if (needCopy(CurSUnit, c, regs, ops)) {
          DEBUG(dbgs() << "**** Need XCC (#: " << regs.size() << ")\n");
          if (UseXPath && ops.size() == 1 && supportsXPath(CurSUnit, ops[0]) &&
              FUSched->XPathOnSideAvailable(c)) {
            DEBUG(dbgs() << "**** Instruction can use XPath\n");
            xpath = true;
            // XXX load/store ?
          } else {
            copy_scheduled = FUSched->ScheduleXCC(c);
            if (copy_scheduled) {
              // insert COPY instructions
              for (int i = 0, e = regs.size(); i < e; ++i) {
                unsigned newreg = insertCopy(regs[i], c);
                CurSUnit->getInstr()->getOperand(ops[i]).setReg(newreg);
              }
              preAssigned[CurSUnit] = ClusterPriority(c);
            }
            break;
          }
        }

        scheduled = FUSched->ScheduleOnSide(CurSUnit, c);
      }

      if (scheduled) {
        FoundSUnit = CurSUnit;
        if (xpath) FUSched->setXPath(CurSUnit->getInstr(), true);
        break;
      }

      if (copy_scheduled)
        WaitingForCopy.push_back(CurSUnit);
      else
        NotReady.push_back(CurSUnit);
    }

    // Add the nodes that aren't ready back onto the available list.
    if (NotReady.size() || WaitingForCopy.size()) {
      AvailableQueue.push_all(NotReady);
      AvailableQueue.push_all(WaitingForCopy);
      NotReady.clear();
      WaitingForCopy.clear();
    }

    // If we found a node to schedule...
    if (FoundSUnit) {
      // node was scheduled on an FU, now put it in the sequence
      ScheduleNodeTopDown(FoundSUnit, CurCycle, cp.getLast());
    } else {
      DEBUG(dbgs() << "*** Finished cycle " << CurCycle << '\n');
      FUSched->AdvanceCycle();
      ++CurCycle;
    }
  }
}

bool UAS::neverHazard(SUnit *SU) {
  switch (SU->getInstr()->getOpcode()) {
  case TargetOpcode::COPY:
  {
    // XXX copies need to be assigned too!
    unsigned srcReg = SU->getInstr()->getOperand(1).getReg();
    unsigned dstReg = SU->getInstr()->getOperand(0).getReg();
    if (TRI->isVirtualRegister(dstReg)) {
      if (TRI->isVirtualRegister(srcReg))
        // vreg -> vreg copies do not get assigned (eg. aReg -> predReg)
        return true;

      const TargetRegisterClass *RC = ARegsRegisterClass;
      MRI.setRegClass(dstReg, RC);
      DEBUG(dbgs() << "**** RC assigned [" << RC->getName() << "]: ");
      DEBUG(SU->dump(this));
    }
    return true;
  }
  }
  return false;
}

bool UAS::XccStall(SUnit *SU, unsigned side, unsigned CurCycle) {
  for (SUnit::pred_iterator I = SU->Preds.begin(), E = SU->Preds.end();
       I != E; ++I) {
    SUnit *PredSU = I->getSUnit();
    // pre-conditions for an XPATH stall:
    // 1. it's a true dependency
    // 2. predecessor has latency, ie. it's a real instruction
    // 3. dest cluster is different from the one of the predecessor
    // 4. predecessor is not a load
    if (I->getKind() == SDep::Data && I->getLatency() &&
        side != getAssignedSide(PredSU) &&
        !(PredSU->getInstr()->getDesc().TSFlags & TMS320C64XII::is_memaccess))
    {
      int slack = CurCycle - (PredSU->getDepth() + PredSU->Latency);
      if (slack == 0) {
        DEBUG(dbgs() << "**** XCC Stall: SU(" << SU->NodeNum << ") --> SU("
              << PredSU->NodeNum << ") = " << slack << "\n");
        ++XccStalls;
        return true;
      }
    }
  }
  return false;
}

unsigned UAS::getAssignedSide(SUnit *SU) {
  // we rewrite cluster sides during assignment
  return TMS320C64XInstrInfo::getSide(SU->getInstr());
}

unsigned UAS::getVRegSide(unsigned reg) {
  // look up whether vreg has been remapped
  const TargetRegisterClass *RC = MRI.getRegClass(reg);
  if (RC == ARegsRegisterClass || RC->hasSuperClass(ARegsRegisterClass))
    return 0; // ASide
  else if (RC == BRegsRegisterClass || RC->hasSuperClass(BRegsRegisterClass))
    return 1; // BSide

  DEBUG(dbgs() << "Cannot determine cluster for reg in class: "
        << RC->getName() << "\n");
  DEBUG(dbgs() << "live-in: " << MRI.isLiveIn(reg) << "\n");
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (MachineRegisterInfo::livein_iterator LI = MRI.livein_begin(),
       LE = MRI.livein_end(); LI != LE; ++LI) {
    DEBUG(dbgs() << PrintReg(LI->second) << " -> arg-"
          << PrintReg(LI->first, TRI) << "\n");
  }

  llvm_unreachable("reg side mishap");
  return -1;
}

unsigned UAS::getVRegOnSide(unsigned reg, unsigned side) {
  // reg on requested side, return original
  if (getVRegSide(reg) == side)
    return reg;
  // otherwise check for a copy
  return CAState->getXccVReg(reg, side);
}

bool UAS::needCopy(SUnit *SU, unsigned side, SmallVector<unsigned,2> &regs,
                   SmallVector<unsigned,2> &ops) {
  const MachineInstr *MI = SU->getInstr();
  const TargetInstrDesc desc = MI->getDesc();

  // don't require XCC for COPIES
  if (MI->getOpcode() == TargetOpcode::COPY)
    return false;

  for (unsigned i = 0, e = std::max(
      MI->findFirstPredOperandIdx(), (int) desc.NumOperands); i < e; ++i) {
    const MachineOperand &op = MI->getOperand(i);
    if (op.isReg() && op.isUse()) {
      int reg = op.getReg();
      if (TargetRegisterInfo::isVirtualRegister(reg)) {
        const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
        const TargetRegisterClass *OpRC = TOI.getRegClass(TRI);

        // Predicate register can be used from both cluster sides.
        if (OpRC == PredRegsRegisterClass)
          continue;

        const TargetRegisterClass *RegRC = MRI.getRegClass(reg);

        // XXX most likely from a COPY, which still needs to be handled
        if (RegRC == GPRegsRegisterClass)
          continue;

        // XXX when the vreg is a pred reg, we don't know its side and need a
        // copy to be safe
        if (RegRC == PredRegsRegisterClass) {
          if (CAState->getXccVReg(reg, side))
            continue; // copy is already there
          else {
            regs.push_back(reg);
            ops.push_back(i);
          }
        // is the register available on this side?
        } else if (!getVRegOnSide(reg, side)) {
          regs.push_back(reg); // we need to copy it
          ops.push_back(i);
        }
      }
    }
  }
  return regs.size();
}

unsigned UAS::insertCopy(unsigned reg, unsigned side) {
  unsigned vnew = MRI.createVirtualRegister(sideToRC(side));
  MachineInstr *DefMI = MRI.getVRegDef(reg);
  MachineBasicBlock *DefMBB = DefMI->getParent();
  DebugLoc dl;
  MachineInstr *MI;
  // place the copy
  bool defInDifferentBlock = DefMI->getParent() != BB;
  if (defInDifferentBlock) {
    // different block
    MachineBasicBlock::iterator InsertIt = DefMI;
    if (DefMI->isPHI())
      InsertIt = DefMBB->getFirstNonPHI(); // don't stick it between PHIs
    else
      ++InsertIt; // insert after def

    MI = BuildMI(*DefMI->getParent(), InsertIt, dl,
                 TII->get(TargetOpcode::COPY), vnew).addReg(reg);
  } else {
    // same block, just create the MI
    MI = BuildMI(MF, dl, TII->get(TargetOpcode::COPY), vnew).addReg(reg);
  }
  SUnit *SU = new SUnit(MI, SUnits.size() + CopySUs.size());
  SU->OrigNode = SU;

  // insert copy into scheduling sequence of the current block
  if (!defInDifferentBlock) {
    CopySUs.push_back(SU);
    Sequence.push_back(SU);
  }

  CAState->addXccSplit(reg, vnew, side, MI);
  DEBUG(dbgs() << "*** Scheduling [XCC]: ");
  DEBUG(SU->dump(this));
  XccInserted++;
  return vnew;
}

bool UAS::supportsXPath(SUnit *SU, unsigned opNum) {
  const MachineInstr *MI = SU->getInstr();
  const TargetOperandInfo &TOI = MI->getDesc().OpInfo[opNum];
  const TargetRegisterClass *RC = TOI.getRegClass(TRI);
  return (RC == GPRegsRegisterClass);
}

const TargetRegisterClass *UAS::sideToRC(unsigned side) const {
  if (side == 0)
    return ARegsRegisterClass;
  else
    return BRegsRegisterClass;
}

bool UAS::rewriteAssignedCluster(SUnit *SU, unsigned side) {
  const TMS320C64XInstrInfo *tii = static_cast<const TMS320C64XInstrInfo*>(TII);
  MachineInstr *MI = SU->getInstr();

  if (!TMS320C64XInstrInfo::isFlexible(MI))
    return false;

  int newOpc = tii->getSideOpcode(MI->getOpcode(), side);
  if (newOpc != MI->getOpcode()) {
    const TargetInstrDesc &desc = TII->get(newOpc);
    MI->setDesc(desc);
    return true;
  }
  return false;
}

void UAS::rewriteDefs(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();
  MachineOperand &MO = MI->getOperand(0);
  if (!MO.isReg() || !MO.isDef()
      || !TargetRegisterInfo::isVirtualRegister(MO.getReg())
      || MRI.getRegClass(MO.getReg()) != GPRegsRegisterClass)
    return;

  if (MI->getOpcode() == COPY) {
    MachineOperand &src = SU->getInstr()->getOperand(1);
    assert(MRI.getRegClass(src.getReg()) != GPRegsRegisterClass);
    MRI.setRegClass(MO.getReg(), MRI.getRegClass(src.getReg()));
    CAState->addVChange(MO.getReg(), MRI.getRegClass(src.getReg()));
    return;
  }

  MRI.setRegClass(MO.getReg(), ARegsRegisterClass);
  CAState->addVChange(MO.getReg(), ARegsRegisterClass);
}

void UAS::rewriteUses(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();
  const TargetInstrDesc desc = MI->getDesc();
  for (unsigned i = 0, e = std::min((unsigned) desc.NumOperands, MI->getNumOperands());
       i != e; ++i) {
    const TargetOperandInfo &TOI = desc.OpInfo[i];
    MachineOperand &MO = MI->getOperand(i);

    if (MO.isReg() && MO.isUse()) {
      unsigned reg = MO.getReg();

      // the register class required by the instruction
      const TargetRegisterClass *RCrequired = TOI.getRegClass(TRI);
      // may not have register class associated (eg. COPY)
      if (!RCrequired)
        continue;

      // predicate registers can be read from both sides
      if (RCrequired == PredRegsRegisterClass)
        continue;

      const TargetRegisterClass *RCreg;
      if (TargetRegisterInfo::isVirtualRegister(reg))
        RCreg = MRI.getRegClass(reg);
      else
        RCreg = TMS320C64X::BRegsRegClass.contains(reg) ? BRegsRegisterClass :
          ARegsRegisterClass;

      if ((RCrequired != RCreg) && !RCrequired->hasSubClass(RCreg)) {
        DEBUG(dbgs() << "RCs disagree at operand " << MO
              << ", req: " << RCrequired->getName()
              << ", reg: " << RCreg->getName() << "\n");
        unsigned newreg = CAState->getXccVReg(reg, RCrequired);
        assert(newreg);
        MO.setReg(newreg);
        DEBUG(dbgs() << "**** Operand rewrite: ");
        DEBUG(SU->dump(this));
      }
    }
  }
}


ClusterPriority UAS::getClusterPriority(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();

  // only one cluster available when we cannot assign
  if (!TMS320C64XInstrInfo::isFlexible(MI)) {
    return ClusterPriority(TMS320C64XInstrInfo::getSide(MI));
  }

  switch (PF) {
  case None:
    //return ClusterPriority(TMS320C64XInstrInfo::getSide(MI));
    return ClusterPriority(0, 1);
  case Random:
    return prioRandom(MI);
  case MWP:
  default:
    return prioMWP(MI);
  }
}

ClusterPriority UAS::prioRandom(MachineInstr *MI) {
  if (rand_r(&RandState) % 2)
    return ClusterPriority(0, 1);
  else
    return ClusterPriority(1, 0);
}

ClusterPriority UAS::prioMWP(MachineInstr *MI) {
  std::pair<int,int> opcnt = countOperandSides(MI, MRI);
  if (opcnt.second > opcnt.first) {
    return ClusterPriority(1, 0);
  } else {
    return ClusterPriority(0, 1);
  }
}

std::pair<int,int>
TMS320C64X::countOperandSides(const MachineInstr *MI,
                              MachineRegisterInfo &MRI) {
  int opcntA = 0, opcntB = 0;
  for (int i = 1,
       e = std::min(MI->getNumOperands(), (unsigned) MI->getDesc().NumOperands);
       i < e; ++i) {
    const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
    if (TOI.isPredicate())
      continue;
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    const TargetRegisterClass *sideRC = MRI.getRegClass(MO.getReg());
    if (sideRC == ARegsRegisterClass)
      opcntA++;
    else if (sideRC == BRegsRegisterClass)
      opcntB++;
  }
  return std::make_pair(opcntA, opcntB);
}

ClusterDAG *TMS320C64X::createClusterDAG(AssignmentAlgorithm AA,
                                         MachineFunction &MF,
                                         const MachineLoopInfo &MLI,
                                         const MachineDominatorTree &MDT,
                                         AssignmentState *state) {
  // everything is UAS right now
  UAS *uas = new UAS(MF, MLI, MDT, state);

  switch(AA) {
  case ClusterUAS:
    uas->setPriority(UAS::MWP); // default
    break;
  case ClusterUAS_none:
    uas->setPriority(UAS::None);
    break;
  case ClusterUAS_rand:
    uas->setPriority(UAS::Random);
    break;
  default:
    llvm_unreachable("not a DAG based algorithm");
  }

  return uas;
}

