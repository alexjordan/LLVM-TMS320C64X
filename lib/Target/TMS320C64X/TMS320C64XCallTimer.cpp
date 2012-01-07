//===-- TMS320C64XCallTimer.cpp - Exclude extsym calls from timing. -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an extsym call timing pass for the TMS320C64X target.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "calltimer"
#include "TMS320C64X.h"
#include "TMS320C64XTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineProfileAnalysis.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TailReplication.h"
#include "llvm/Target/TargetInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCSymbol.h"
#include <list>

using namespace llvm;

STATISTIC(NumProcessedCallsStat, "Number of processed call instructions");

//------------------------------------------------------------------------------

namespace llvm {

class TMS320C64XCallTimer : public MachineFunctionPass {

  private:

    const TMS320C64XInstrInfo *TII;
    unsigned NumProcessedCalls;

  public:

    static char ID;

    // for the init-macro to shut up
    TMS320C64XCallTimer();

    // actual object ctor used for creating a pass instance
    TMS320C64XCallTimer(TMS320C64XTargetMachine &tm);
    ~TMS320C64XCallTimer() {}

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    virtual const char *getPassName() const {
      return "Libcall overhead timing pass for the TMS320C64X target";
    }

    virtual bool runOnMachineFunction(MachineFunction &MF);
    bool runOnMachineBasicBlock(MachineBasicBlock &MBB);
};

char TMS320C64XCallTimer::ID = 0;

} // end of anonymous namespace

//------------------------------------------------------------------------------

#if 0
INITIALIZE_PASS_BEGIN(TMS320C64XCallTimer,
  "call-runtimer", "TMS320C64X call runtimer pass", false, false)
INITIALIZE_PASS_END(TMS320C64XCallTimer,
  "call-runtimer", "TMS320C64X call runtimer pass", false, false)
#endif

//------------------------------------------------------------------------------

FunctionPass *
llvm::createTMS320C64XCallTimerPass(TMS320C64XTargetMachine &tm) {
  return new TMS320C64XCallTimer(tm);
}

//------------------------------------------------------------------------------

TMS320C64XCallTimer::TMS320C64XCallTimer()
: MachineFunctionPass(ID),
  TII(0)
{
  //initializeTMS320C64XCallTimerPass(*PassRegistry::getPassRegistry());
}

//------------------------------------------------------------------------------

TMS320C64XCallTimer::TMS320C64XCallTimer(TMS320C64XTargetMachine &TM)
: MachineFunctionPass(ID),
  TII(TM.getInstrInfo())
{
  //initializeTMS320C64XCallTimerPass(*PassRegistry::getPassRegistry());
}

//------------------------------------------------------------------------------

bool TMS320C64XCallTimer::runOnMachineFunction(MachineFunction &MF) {

  DEBUG(dbgs() << "Run 'TMS320C64XCallTimer' pass for '"
               << MF.getFunction()->getNameStr() << "'\n");

  NumProcessedCalls = 0;

  bool Changed = false;
  for (MachineFunction::iterator FI = MF.begin(), FE = MF.end();
       FI != FE; ++FI) {
    Changed |= runOnMachineBasicBlock(*FI);
  }

  // XXX we could cover more calls at this stage (softfloat, malloc, etc.)

#if 0
  if (MF.getFunction()->getNameStr() == "main") {

    // insert an initializer for the aggregate value here
    MachineFunction::iterator MBB = MF.begin();
    MachineBasicBlock::iterator MI = MBB->begin();

    TMS320C64XInstrInfo::addFormOp(
      TII->addDefaultPred(BuildMI(*MBB, MI, DebugLoc(),
        TII->get(TMS320C64X::mvk_2), TMS320C64X::B31).addImm(0)),
          TMS320C64XII::unit_l, false);

    BuildMI(*MBB, MI, DebugLoc(),
      TII->get(TMS320C64X::mvc_galoises_b), TMS320C64X::GPLYB)
        .addReg(TMS320C64X::B31);

    // finally kick on time stamping, written value is ignored
    BuildMI(*MBB, MI, DebugLoc(),
      TII->get(TMS320C64X::mvc_time_b), TMS320C64X::TSCL)
        .addReg(TMS320C64X::B31, RegState::ImplicitDefine);

    dbgs() << "TMS320C64XCallTimer: 'main' function found!\n";
  }

  // now scan  the entire function looking for call instructions to ext syms
  for (MachineFunction::iterator MFI = MF.begin(); MFI != MF.end(); ++MFI) {

    MachineBasicBlock &MBB = *MFI;
    MachineBasicBlock::iterator MBBI;

    for (MBBI = MFI->begin(); MBBI != MFI->end(); ++MBBI) {

      if (!MBBI->getDesc().isCall()) continue;

      assert(MBBI->getNumOperands() && "Call within operands!");

      // look for external symbols for now only, TODO, check properly
      MachineOperand &callMO = MBBI->getOperand(0);
      if (!callMO.isSymbol()) continue;

      /// NKim, here the way it looks like
      ///
      /// ...
      /// B31 = TSCL     // get the stamp before call
      /// GFPGFR = B31   // save across the call, B31 is not preserved
      /// call ...
      /// B30 = TSCL     // get the stamp after the call
      /// B31 = GFPGFR   // get the stamp before the call
      /// B30 = B30-B31  // compute time for the call
      /// B31 = GPLYB    // get aggregate so far
      /// B30 = B31+B30  // update aggregate value
      /// GPLYB = B30    // save new aggregate value

      // B31 = TSCL, get the stamp before the call instruction
      MachineInstr *readTimeStampBeforeCall = BuildMI(MBB, MBBI, DebugLoc(),
        TII->get(TMS320C64X::mvc_b_time), TMS320C64X::B31)
          .addReg(TMS320C64X::TSCL, RegState::ImplicitDefine);

      DEBUG(dbgs() << "Stamp before the call: " << *readTimeStampBeforeCall);

      // GFPGFR = B31, save across the call, since B31 may be destroyed 
      MachineInstr *copyTimeStampBeforeCall = BuildMI(MBB, MBBI, DebugLoc(),
        TII->get(TMS320C64X::mvc_galoises_b), TMS320C64X::GFPGFR)
          .addReg(TMS320C64X::B31);

      DEBUG(dbgs() << "Save stamp across call: " << *copyTimeStampBeforeCall);

      MachineBasicBlock::iterator pos = MBBI; ++pos;

      // B30 = TSCL, get the new time stamp, after the call
      MachineInstr *readTimeStampAfterCall = BuildMI(MBB, pos, DebugLoc(),
        TII->get(TMS320C64X::mvc_b_time), TMS320C64X::B30)
          .addReg(TMS320C64X::TSCL, RegState::ImplicitDefine);

      DEBUG(dbgs() << "Stamp after the call: " << *readTimeStampAfterCall);

      // B31 = GFPGFR, restore the stamp got before the call
      MachineInstr *oldStampCopy = BuildMI(MBB, pos, DebugLoc(),
        TII->get(TMS320C64X::mvc_b_galoises), TMS320C64X::B31)
          .addReg(TMS320C64X::GFPGFR, RegState::ImplicitDefine);

      DEBUG(dbgs() << "Restore old stamp: " << *oldStampCopy);

      // B30 = B30 - B31, compute time required for the call
      MachineInstr *timeStampDiff = TMS320C64XInstrInfo::addFormOp(
        TII->addDefaultPred(BuildMI(MBB, pos, DebugLoc(),
          TII->get(TMS320C64X::sub_rr_2), TMS320C64X::B30)
            .addReg(TMS320C64X::B30).addReg(TMS320C64X::B31)),
              TMS320C64XII::unit_s, false);

      DEBUG(dbgs() << "Time stamp difference: " << *timeStampDiff);

      // B31 = GPLYB, get the aggregated value so far
      MachineInstr *aggregateOld = BuildMI(MBB, pos, DebugLoc(),
          TII->get(TMS320C64X::mvc_b_galoises), TMS320C64X::B31)
            .addReg(TMS320C64X::GPLYB, RegState::ImplicitDefine);

      DEBUG(dbgs() << "Old aggregate value: " << *aggregateOld);

      // B30 = B31 + B30, increment the aggregate value
      MachineInstr *aggregateCount = TMS320C64XInstrInfo::addFormOp(
        TII->addDefaultPred(BuildMI(MBB, pos, DebugLoc(),
          TII->get(TMS320C64X::add_rr_2), TMS320C64X::B30)
            .addReg(TMS320C64X::B31).addReg(TMS320C64X::B30)),
              TMS320C64XII::unit_s, false);

      DEBUG(dbgs() << "Aggregate increment: " << *aggregateCount);

      // GPLYB = B30, save the new aggregate value !
      MachineInstr *aggregateNew = BuildMI(MBB, pos, DebugLoc(),
        TII->get(TMS320C64X::mvc_galoises_b), TMS320C64X::GPLYB)
          .addReg(TMS320C64X::B30);

      DEBUG(dbgs() << "New aggregate value: " << *aggregateNew);

      ++NumProcessedCalls;
    }
  }
#endif

  NumProcessedCallsStat += NumProcessedCalls;
  return Changed;
}

bool TMS320C64XCallTimer::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  DebugLoc dl;
  bool Changed = false;

  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E; ++I) {
    if (I->getOpcode() == TMS320C64X::mvc_start) {
      // remove from instruction list
      MachineInstr *start = MBB.remove(I++);
      while (!I->getDesc().isCall()) {
        assert(I != MBB.end());
        ++I;
      }
      // insert right before call
      MBB.insert(I, start);
      MachineBasicBlock::iterator callIt = I;

      while (I->getOpcode() != TMS320C64X::mvc_end) {
        assert(I != MBB.end());
        ++I;
      }
      // remove and insert right after call
      MachineInstr *end = MBB.remove(I++);
      MBB.insertAfter(callIt, end);

      ++NumProcessedCalls;
      Changed |= true;
      --I;
    }
  }

  return Changed;
}
