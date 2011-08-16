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

#include "TMS320C64XClusterAssignment.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Debug.h"
#undef DEBUG
#define DEBUG(x) x

using namespace llvm;
namespace C64XII = TMS320C64XII;

namespace {
// register class narrowing
const TargetRegisterClass *resolveRC(const TargetRegisterClass *Actual,
                                     const TargetRegisterClass *Required) {
  // restrict to A or B regs if required
  if (Required == TMS320C64X::ARegsRegisterClass ||
      Required == TMS320C64X::BRegsRegisterClass)
    return Required;

  // no restriction when only GP reg class is required
  assert(Required == TMS320C64X::GPRegsRegisterClass);
  return Actual;
}

/// test assignment algorithm - assigns everything possible to B side
struct BSideAssigner : public TMS320C64XClusterAssignment {

  BSideAssigner(TargetMachine &tm) : TMS320C64XClusterAssignment(tm) {}

  virtual void assignBasicBlock(MachineBasicBlock *MBB);

  int select(int side, const res_set_t &set);
};
}



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

bool TMS320C64XClusterAssignment::runOnMachineFunction(MachineFunction &Fn) {
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
          resolveRC(RCis, TOI.getRegClass(TRI));

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
          resolveRC(RCvreg, TOI.getRegClass(TRI));

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

FunctionPass *llvm::createTMS320C64XClusterAssignment(TargetMachine &tm) {
  return new BSideAssigner(tm);
}

