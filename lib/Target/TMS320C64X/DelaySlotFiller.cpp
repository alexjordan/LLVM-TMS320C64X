//===-- DelaySlotFiller.cpp - TMS320C64X delay slot filler, stolen from Sparc //
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "delayslotfiller"
#include "TMS320C64X.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

namespace {
  struct Filler : public MachineFunctionPass {
    TargetMachine &TM;
    const TargetInstrInfo *TII;

    static char ID;

    Filler(TargetMachine &tm)
    : MachineFunctionPass(ID),
      TM(tm),
      TII(tm.getInstrInfo())
    {}

    virtual const char *getPassName() const {
      return "TMS320C64X Delay Slot Filler";
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

  char Filler::ID = 0;

} // end of anonymous namespace

/// createTMS320C64XDelaySlotFillerPass - Returns a pass that fills in delay
/// slots in TMS320C64X MachineFunctions
///

FunctionPass *llvm::createTMS320C64XDelaySlotFillerPass(TargetMachine &tm) {
  return new Filler(tm);
}

/// runOnMachineBasicBlock - Fill in delay slots for the given basic block.
/// Currently, we fill delay slots with NOPs. We insert 4 nops
///

bool Filler::runOnMachineBasicBlock(MachineBasicBlock &MBB) {

  int delay;
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ++I)
    if (I->getDesc().hasDelaySlot()) {
      MachineBasicBlock::iterator J = I;
      delay = GET_DELAY_SLOTS(I->getDesc().TSFlags);
      ++J;
      TMS320C64XInstrInfo::addDefaultPred(BuildMI(MBB, J, I->getDebugLoc(),
        TII->get(TMS320C64X::noop)).addImm(delay));

      Changed = true;
    }
  return Changed;
}
