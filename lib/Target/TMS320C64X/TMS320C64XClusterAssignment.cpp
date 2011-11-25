//===-- TMS320C64XClusterAssignment.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Cluster assignment for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cluster-assignment"
#include "TMS320C64XClusterAssignment.h"
#include "TMS320C64XInstrInfo.h"
#include "ClusterDAG.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::TMS320C64X;

namespace C64XII = TMS320C64XII;

namespace {

/// helper that does register class narrowing - if required is more specific
/// return required, otherwise actual.
const TargetRegisterClass *narrowRC(const TargetRegisterClass *Actual,
                                    const TargetRegisterClass *Required) {
  // restrict to A or B regs if required
  if (Required == TMS320C64X::ARegsRegisterClass ||
      Required == TMS320C64X::BRegsRegisterClass)
    return Required;

  // no restriction when only GP reg class is required
  assert(Required == TMS320C64X::GPRegsRegisterClass);
  return Actual;
}

/// abstract base for basic block assignment
struct BBAssign : public TMS320C64XClusterAssignment {

  BBAssign(TargetMachine &tm) : TMS320C64XClusterAssignment(tm) {}

  bool runOnMachineFunction(MachineFunction &Fn);

  // concrete assignment algorithms override these
  virtual void assignBasicBlock(MachineBasicBlock *MBB) = 0;
};

/// null assignment algorithm - does nothing
struct NoAssign : public BBAssign {
  NoAssign(TargetMachine &tm) : BBAssign(tm) {}
  virtual void assignBasicBlock(MachineBasicBlock *MBB) {}
};

/// test assignment algorithm - assigns everything possible to B side
struct BSideAssigner : public BBAssign {

  BSideAssigner(TargetMachine &tm) : BBAssign(tm) {}

  virtual void assignBasicBlock(MachineBasicBlock *MBB);

  int select(int side, const res_set_t &set);
};

/// assignment pass using a ScheduleDAG based scheduler class
struct DagAssign : public TMS320C64XClusterAssignment {

  /// algorithm for selecting a scheduler
  AssignmentAlgorithm Algo;

  /// the actual scheduler
  ClusterDAG *Scheduler;

  DagAssign(TargetMachine &tm, AssignmentAlgorithm algo)
    : TMS320C64XClusterAssignment(tm)
    , Algo(algo)
  {}

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &Fn);
  void applyXccUses(MachineFunction &Fn, const AssignmentState &State);
  void assignPHIs(AssignmentState *State, MachineBasicBlock *MBB);
};
}

static cl::opt<AssignmentAlgorithm>
ClusterOpt("c64x-clst",
  cl::desc("Choose a cluster assignment algorithm"),
  cl::NotHidden,
  cl::values(
    clEnumValN(ClusterNone, "none", "Do not assign clusters"),
    clEnumValN(ClusterB, "bside", "Assign everything to cluster B"),
    clEnumValN(ClusterUAS, "uas", "Unified assign and schedule"),
    clEnumValN(ClusterUAS_none, "uas-none", "UAS (no priority: A before B)"),
    clEnumValN(ClusterUAS_rand, "uas-rand", "UAS (random cluster priority)"),
    clEnumValN(ClusterUAS_mwp, "uas-mwp", "UAS (magnitude weighted pred)"),
    clEnumValEnd),
  cl::init(ClusterNone));

//
// TMS320C64XClusterAssignment implementation
//

char TMS320C64XClusterAssignment::ID = 0;

TMS320C64XClusterAssignment::TMS320C64XClusterAssignment(TargetMachine &tm)
  : MachineFunctionPass(ID)
  , TM(tm)
  , TII(static_cast<const TMS320C64XInstrInfo*>(tm.getInstrInfo()))
  , TRI(tm.getRegisterInfo())
{}

bool BBAssign::runOnMachineFunction(MachineFunction &Fn) {
  MachineRegisterInfo &MRI = Fn.getRegInfo();

  // track vregs with changed register class
  VirtMap_t VirtMap;
  VirtMap.resize(MRI.getNumVirtRegs());

  for (MachineFunction::iterator I = Fn.begin(), E = Fn.end(); I != E; ++I) {
    Assigned.clear();
    assignBasicBlock(I);

#if 1
    for (assignment_t::iterator AI = Assigned.begin(), AE = Assigned.end();
         AI != AE; ++AI) {
      MachineInstr *MI = AI->first;
      DEBUG(dbgs() << *AI->first << " assigned to "
            << TII->res2Str(AI->second) << "\n");

      int newOpc = TII->getSideOpcode(MI->getOpcode(), C64XII::BSide);
      MI->setDesc(TII->get(newOpc));

      for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
        const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
        MachineOperand &MO = MI->getOperand(i);

        if (TOI.isPredicate())
          break;

        if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
          continue;

        const TargetRegisterClass *RCis =  MRI.getRegClass(MO.getReg());
        const TargetRegisterClass *RCshould =
          narrowRC(RCis, TOI.getRegClass(TRI));

        if (RCis != RCshould) {
          if (MO.isDef()) {
            DEBUG(dbgs() << "RC change for "<< PrintReg(MO.getReg()) << ": "
                  << RCis->getName() << " -> " << RCshould->getName() << "\n");
            MRI.setRegClass(MO.getReg(), RCshould);
            VirtMap[MO.getReg()] = RCshould;
          } else {
            fixUseRC(Fn, MI, MO, RCshould);
            VirtMap.resize(MRI.getNumVirtRegs());
          }
        }
      }

      DEBUG(dbgs() << "-- end assignment --\n");
    }
#endif
  }

  verifyUses(Fn, VirtMap);

  return true;
}

void TMS320C64XClusterAssignment::verifyUses(MachineFunction &MF,
                                             VirtMap_t &VirtMap) {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (MachineFunction::iterator I = MF.begin(), E = MF.end(); I != E; ++I) {
    MachineBasicBlock *MBB = I;
    for (MachineBasicBlock::iterator MI = MBB->begin(), ME = MBB->end();
         MI != ME; ++MI) {
      // skip pseudo MIs
      if (MI->getOpcode() <= TargetOpcode::COPY)
        continue;

      for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
        const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
        MachineOperand &MO = MI->getOperand(i);

        if (TOI.isPredicate())
          break;

        if (!MO.isReg() || !MO.isUse()
            || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
          continue;

        const TargetRegisterClass *RCvreg = VirtMap[MO.getReg()];
        if (!RCvreg)
          continue;

        const TargetRegisterClass *RCtgt =
          narrowRC(RCvreg, TOI.getRegClass(TRI));

        if (RCvreg != RCtgt) {
          DEBUG(dbgs() << *MI);
          DEBUG(dbgs() << "RCs disagree at operand " << MO
                << " is: " << RCvreg->getName()
                << " should: " << RCtgt->getName() << "\n");
          fixUseRC(MF, MI, MO, RCtgt);
          VirtMap.resize(MRI.getNumVirtRegs());
        }
      }
    }
  }
}

void TMS320C64XClusterAssignment::fixUseRC(MachineFunction &MF,
                                           MachineInstr *MI,
                                           MachineOperand &MO,
                                           const TargetRegisterClass *RC) {
  // XXX instruction may be commutable
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned vnew = MRI.createVirtualRegister(RC);

  // insert a copy from the original reg to the new one
  BuildMI(*MI->getParent(), MachineBasicBlock::iterator(MI),
          MI->getDebugLoc(), TII->get(TargetOpcode::COPY), vnew)
    .addReg(MO.getReg());
  MO.setReg(vnew);
  DEBUG(dbgs() << "COPY added\n");
}

void TMS320C64XClusterAssignment::assign(MachineInstr *MI, int res) {
  Assigned[MI] = res;
}


void TMS320C64XClusterAssignment::analyzeInstr(MachineInstr *MI,
                                               res_set_t &set) const {
  unsigned opc = MI->getOpcode();
  if (opc == TargetOpcode::COPY) {
    unsigned src = MI->getOperand(1).getReg();
    unsigned dst = MI->getOperand(0).getReg();
    DEBUG(dbgs() << *MI << " copies " << PrintReg(src, TRI) << " to "
          << PrintReg(dst, TRI) << "\n\n");
    return;
  }

  unsigned flags = TII->get(opc).TSFlags;
  unsigned us = flags & TMS320C64XII::unit_support_mask;

  // special case 1: all units are supported by instruction
  if (us == 15) {
    DEBUG(dbgs() << *MI << " can be scheduled anywhere\n\n");
    // return the set empty
    return;
  }

  // special case 2: instruction is fixed
  if (us == 0) {
    unsigned fu = GET_UNIT(flags) << 1;
    fu |= IS_BSIDE(flags) ? 1 : 0;
    DEBUG(dbgs() << *MI << " fixed to " << TII->res2Str(fu)
                 <<  "\n\n");
    set.insert(fu);
    return;
  }

  DEBUG(dbgs() << *MI << " supported by: ");
  // from highest to lowest, the unit support bits are: L S M D
  for (int i = 0; i < TMS320C64XII::NUM_FUS; ++i) {
    if ((us >> i) & 0x1) {
      set.insert(i << 1);
      set.insert((i << 1) + 1);
      DEBUG(dbgs() << TII->res2Str(i << 1) << " "
            << TII->res2Str((i << 1) +1) + " ");
    }
  }
  DEBUG(dbgs() <<  "\n\n");
}

//
// BSideAssigner implementation
//

void BSideAssigner::assignBasicBlock(MachineBasicBlock *MBB) {
  for (MachineBasicBlock::iterator I = MBB->begin(); I != MBB->end(); ++I) {
    if (I->getOpcode() == TargetOpcode::COPY)
      continue;

    // find out where this instruction can execute
    res_set_t supported;
    analyzeInstr(I, supported);

    // select a resource on side B and if possible assign it
    int resource = select(TMS320C64XII::BSide, supported);
    if (resource >= 0)
      assign(I, resource);
  }
}

int BSideAssigner::select(int side, const res_set_t &set) {
  assert(set.size());
  if (set.size() == 1)
    return -1;
  for (res_set_t::const_iterator I = set.begin(), E = set.end(); I != E; ++I)
    if ((*I & 0x1) == side)
      return *I;
  return -1;
}

//
// DAG based assignment implementation
//

bool DagAssign::runOnMachineFunction(MachineFunction &Fn) {
  const MachineLoopInfo &MLI = getAnalysis<MachineLoopInfo>();
  const MachineDominatorTree &MDT = getAnalysis<MachineDominatorTree>();

  AssignmentState State;
  Scheduler = createClusterDAG(Algo, Fn, MLI, MDT, &State);

  // use the post RA hazard recognizer
  ResourceAssignment *RA =
    TMS320C64XInstrInfo::CreateFunctionalUnitScheduler(&TM);
  Scheduler->setFunctionalUnitScheduler(RA);

  for (MachineFunction::iterator MBB = Fn.begin(), MBBe = Fn.end();
       MBB != MBBe; ++MBB) {
    assignPHIs(&State, MBB);
    Scheduler->Run(MBB, MBB->getFirstNonPHI(), MBB->end(), MBB->size());
  }

  Fn.verifySSA(this, "After Cluster Assignment");
  if (DebugFlag && isCurrentDebugType(DEBUG_TYPE))
    Fn.dump();

  delete Scheduler;
  delete RA;
  return true;
}

void DagAssign::assignPHIs(AssignmentState *State, MachineBasicBlock *MBB) {
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  for (MachineBasicBlock::iterator MI = MBB->begin(), ME = MBB->getFirstNonPHI();
       MI != ME; ++MI) {
    assert(MI->isPHI());
    // XXX better way to assign defs by PHI nodes?
    std::pair<int,int> opcnt = countOperandSides(MI, MRI);

    const TargetRegisterClass *RC =
      (opcnt.first > opcnt.second) ? BRegsRegisterClass : ARegsRegisterClass;
    DEBUG(dbgs() << "PHI assign: " << *MI << " [A: " << opcnt.first << ", B: "
          << opcnt.second << "] -> " << RC->getName() << "\n");
    MachineOperand &MO = MI->getOperand(0);
    MRI.setRegClass(MO.getReg(), RC);
    State->addVChange(MO.getReg(), RC);
  }
}

void DagAssign::applyXccUses(MachineFunction &MF, const AssignmentState &State) {
  // XXX this may replace the use fixing in TMS320C64XClusterAssignment above

  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (MachineFunction::iterator I = MF.begin(), E = MF.end(); I != E; ++I) {
    MachineBasicBlock *MBB = I;
    for (MachineBasicBlock::iterator MI = MBB->begin(), ME = MBB->end();
         MI != ME; ++MI) {
      // skip pseudo MIs
      if (MI->getOpcode() <= TargetOpcode::COPY)
        continue;

      for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
        const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
        MachineOperand &MO = MI->getOperand(i);

        if (!MO.isReg() || !MO.isUse()
            || !TargetRegisterInfo::isVirtualRegister(MO.getReg())
            || MO.getReg() == 0)
          continue;

        unsigned reg = MO.getReg();

        const TargetRegisterClass *RCchg = State.getVChange(reg);
        if (RCchg) {
          DEBUG(dbgs() << "reg side-change at operand " << MO << ": ");
          DEBUG(dbgs() << *MI);
        }

        if (TOI.isPredicate())
          break;


        const TargetRegisterClass *RCreg = MRI.getRegClass(reg);

        // enforce predicate register rewrites
        unsigned predReg =
          State.getXccVReg(reg, TMS320C64XInstrInfo::getSide(MI));
        if (predReg && MRI.getRegClass(reg) == PredRegsRegisterClass) {
          MO.setReg(predReg);
          continue;
        }

        // promote to A/B class
        if (RCreg->hasSuperClass(ARegsRegisterClass))
          RCreg = ARegsRegisterClass;
        else if (RCreg->hasSuperClass(BRegsRegisterClass))
          RCreg = BRegsRegisterClass;

        assert(RCreg == ARegsRegisterClass ||
               RCreg == BRegsRegisterClass);

        const TargetRegisterClass *RCinst = TOI.getRegClass(TRI);

        if (RCinst != GPRegsRegisterClass && RCreg != RCinst) {
          DEBUG(dbgs() << *MI);
          DEBUG(dbgs() << "RCs disagree at operand " << MO
                << " is: " << RCreg->getName()
                << ", should be: " << RCinst->getName() << "\n");
          // look for XCC that copies reg to side of this MI
          unsigned newReg =
            State.getXccVReg(MO.getReg(), TMS320C64XInstrInfo::getSide(MI));
          assert(newReg && "missing XCC vreg");
          MO.setReg(newReg);
        }
      }
    }
  }
}

//
// AssignmentState implementation
//

void AssignmentState::addXccSplit(unsigned srcReg, unsigned dstReg,
                                  unsigned dstSide, MachineInstr *copyInst) {
  VXcc[dstSide].grow(dstReg);
  VXcc[dstSide][srcReg] = dstReg;
}

unsigned AssignmentState::getXccVReg(unsigned reg, unsigned side) const {
  // check if this vreg has been mapped to side
  if (!VXcc[side].inBounds(reg))
    return 0;

  // return the XCC version for the given cluster
  return VXcc[side][reg];
}

unsigned
AssignmentState::getXccVReg(unsigned reg, const TargetRegisterClass *RC) const {
  int side = -1;
  if (RC == ARegsRegisterClass)
    side = 0;
  else if (RC == BRegsRegisterClass)
    side = 1;
  assert(side >= 0);
  return getXccVReg(reg, side);
}

void AssignmentState::addVChange(unsigned reg, const TargetRegisterClass *RC) {
  VirtMap.grow(reg);
  VirtMap[reg] = RC;
}

const TargetRegisterClass *AssignmentState::getVChange(unsigned reg) const {
  if (!VirtMap.inBounds(reg))
    return NULL;

  return VirtMap[reg];
}

FunctionPass *llvm::createTMS320C64XClusterAssignment(TargetMachine &tm) {
  switch (ClusterOpt) {
  default: llvm_unreachable("unknown cluster assignment");
  case ClusterNone: return new NoAssign(tm);
  case ClusterB:    return new BSideAssigner(tm);
  case ClusterUAS:
  case ClusterUAS_none:
  case ClusterUAS_rand:
  case ClusterUAS_mwp:
                    return new DagAssign(tm, ClusterOpt);
  }
}

