//===-- TMS320C64X/BranchDelayExpansion.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Handles modeling of branches and their delayed execution.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetSubtarget.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

//#undef DEBUG
//#define DEBUG(x) x
using namespace llvm;
using namespace TMS320C64X;

namespace {
  struct BranchDelayBase : public MachineFunctionPass {
    TargetMachine &TM;
    const TargetInstrInfo *TII;

    BranchDelayBase(char ID, TargetMachine &tm)
      : MachineFunctionPass(ID),
      TM(tm),
      TII(tm.getInstrInfo())
    {}

    virtual bool runOnMachineBasicBlock(MachineBasicBlock &MBB) = 0;
    bool runOnMachineFunction(MachineFunction &F);

  };

  struct BranchDelayExpander : public BranchDelayBase {
    static char ID;

    BranchDelayExpander(TargetMachine &tm)
      : BranchDelayBase(ID, tm) {}

    virtual const char *getPassName() const {
      return "TMS320C64X Branch Delay Expander";
    }
    virtual bool runOnMachineBasicBlock(MachineBasicBlock &MBB);
  };

  struct BranchDelayReducer : public BranchDelayBase {
    static char ID;

    BranchDelayReducer(TargetMachine &tm)
      : BranchDelayBase(ID, tm) {}

    virtual const char *getPassName() const {
      return "TMS320C64X Branch Delay Reducer";
    }
    virtual bool runOnMachineBasicBlock(MachineBasicBlock &MBB);
  };

  char BranchDelayExpander::ID = 0;
  char BranchDelayReducer::ID = 0;
}

bool BranchDelayBase::runOnMachineFunction(MachineFunction &F) {
  bool Changed = false;
  for (MachineFunction::iterator FI = F.begin(), FE = F.end();
       FI != FE; ++FI)
    Changed |= runOnMachineBasicBlock(*FI);
  return Changed;
}

bool BranchDelayExpander::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  DebugLoc dl;
  bool Changed = false;

  for (MachineBasicBlock::iterator I = MBB.getFirstTerminator(), E = MBB.end();
       I != E;) {

    // a branch to the following block is elimited (becomes fallthrough)
    if (I->getOpcode() == TMS320C64X::branch &&
        MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
      MBB.erase(I++);
      continue;
    }

    // remember where the branch is
    MachineBasicBlock::iterator BI = I;

    // insert the pseudo branch prepare
    MachineInstrBuilder MIB =
      BuildMI(MBB, ++I, dl, TII->get(BR_PREPARE));

    // add the branch opcode and operands to it
    MIB.addImm(BI->getOpcode());
    for (unsigned i = 0, e = BI->getNumOperands(); i < e; ++i)
      MIB.addOperand(BI->getOperand(i));

    BuildMI(MBB, I, dl, TII->get(BR_OCCURS));
    MBB.erase(BI);
    Changed |= true;
  }

  return Changed;
}

bool BranchDelayReducer::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  DebugLoc dl;
  bool Changed = false;

  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
       I != E;) {
    switch (I->getOpcode()) {
    default:
      ++I;
      continue;
    case BR_OCCURS:
      MBB.erase(I++);
      continue;
    case BR_PREPARE:
      break;
    }

    MachineInstrBuilder MIB =
      BuildMI(&MBB, dl, TII->get(I->getOperand(0).getImm()));

    for (unsigned i = 1, e = I->getNumOperands(); i < e; ++i)
      MIB.addOperand(I->getOperand(i));

    MBB.erase(I++);
    Changed |= true;
  }

  return Changed;
}

FunctionPass *llvm::createTMS320C64XBranchDelayExpander(TargetMachine &tm) {
  return new BranchDelayExpander(tm);
}
FunctionPass *llvm::createTMS320C64XBranchDelayReducer(TargetMachine &tm) {
  return new BranchDelayReducer(tm);
}
