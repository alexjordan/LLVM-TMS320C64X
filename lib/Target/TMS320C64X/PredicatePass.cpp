//===-- DelaySlotFiller.cpp - TMS320C64X delay slot filler, stolen from Sparc //
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
  struct Predicator : public MachineFunctionPass {
    TargetMachine &TM;
    const TargetInstrInfo *TII;

    static char ID;
    Predicator(TargetMachine &tm)
      : MachineFunctionPass(ID), TM(tm), TII(tm.getInstrInfo()) { }

    virtual const char *getPassName() const {
      return "TMS320C64X Delay Slot Filler";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineBasicBlock(MachineBasicBlock &MBB);
    bool runOnMachineFunction(MachineFunction &F) {
      bool Changed = false;
      for (MachineFunction::iterator FI = F.begin(), FE = F.end();
           FI != FE; ++FI)
        Changed |= runOnMachineBasicBlock(*FI);
      return Changed;
    }

  };
  char Predicator::ID = 0;
} // end of anonymous namespace

namespace llvm {
FunctionPass *createTMS320C64XPredicatePass(TargetMachine &tm) {
  return new Predicator(tm);
}
}

bool Predicator::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  using namespace TMS320C64X;

	MachineFunction &MF = *MBB.getParent();
	MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineDominatorTree& MDT = getAnalysis<MachineDominatorTree>();

  bool Changed = false;
  MachineBasicBlock::reverse_iterator I;
  // find the select
  for (I = MBB.rbegin(); I != MBB.rend(); ++I)
    if (I->getOpcode() == mvselect) {
      dbgs() << "found: ";
      I->dump();
      break;
    }
  if (I == MBB.rend())
    return Changed;

  MachineInstr *select = &*I;
  MachineOperand &op = select->getOperand(2);
  assert(op.isUse());
  unsigned reg = op.getReg();
  MachineInstr *def = MRI.getVRegDef(reg);
  assert(def);
  def->dump();

  // now find what defines the true-value
  for (; I != MBB.rend(); ++I) {
    if (MDT.dominates(&*I, def)) {
      dbgs() << "dominator: ";
      I->dump();
    }
  }

  return Changed;
}
