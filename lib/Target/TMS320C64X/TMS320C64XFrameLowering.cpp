//===- TMS320C64X.cpp - TMS320C64X Frame Lowering Information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the TMS320C64X impl. of the TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64XFrameLowering.h"
#include "TMS320C64XInstrInfo.h"
#include "TMS320C64XTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

//-----------------------------------------------------------------------------

bool
TMS320C64XFrameLowering::spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const std::vector<CalleeSavedInfo> &CSI,
                                        const TargetRegisterInfo *TRI) const
{
  const MachineFunction *MF;
  unsigned int i, reg;
  bool is_kill;

  MF = MBB.getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();

  for (i = 0; i < CSI.size(); ++i) {

    // Should this be a kill? Unfortunately the argument registers
    // and nonvolatile registers on the target overlap, which leads
    // to a situation where we spill a nonvolatile register,
    // killing it, and then try and use it as an argument register
    // -> "Using undefined register". So, check whether this reg is
    // a _function_ LiveIn too.

    is_kill = true;
    reg = CSI[i].getReg();

    MachineRegisterInfo::livein_iterator li = MRI.livein_begin();

    for (; li != MRI.livein_end(); li++) {
      if (li->first == reg) {
        is_kill = false;
        break;
      }
    }

    MBB.addLiveIn(reg);

    const TargetInstrInfo &TII = *(MF->getTarget().getInstrInfo());

    // register class and target reg info unused
    TII.storeRegToStackSlot(
      MBB, MBBI, reg, is_kill, CSI[i].getFrameIdx(), 0, 0);
  }

  return true;
}

//-----------------------------------------------------------------------------

void TMS320C64XFrameLowering::emitPrologue(MachineFunction &MF) const {

  int frame_size;

  MachineBasicBlock &MBB = MF.front();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();

  DebugLoc dl = (MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc());

  // Mark return address as being a live in - don't mark it as such for
  // the whole function, because we want to save it manually. Otherwise
  // extra code will be generated to store it elsewhere.
  // Ideally we don't need to save manually, but I call this easier
  // to debug.
  MBB.addLiveIn(TMS320C64X::B3);
  frame_size = MFI->getStackSize();
  frame_size += 8;

  // Align the size of the stack - has to remain double word aligned.
  frame_size += 7;
  frame_size &= ~7;

  const TMS320C64XInstrInfo &TII = *TM.getInstrInfo();

  // Different to epilogue, we always use the prolog pseudo function, which
  // will emit a 2- or 3-cycle prologue.
  TMS320C64XInstrInfo::addDefaultPred(BuildMI(MBB, MBBI, dl,
    TII.get(TMS320C64X::prolog)).addImm(frame_size));
}

//-----------------------------------------------------------------------------

void TMS320C64XFrameLowering::emitEpilogue(MachineFunction &MF,
                                           MachineBasicBlock &MBB) const
{
  DebugLoc DL;

  const MachineFrameInfo *MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = prior(MBB.end());

  if (MFI->hasVarSizedObjects())
    llvm_unreachable("Can't currently support varsize stack frame");

  if (MBBI->getOpcode() != TMS320C64X::ret)
    llvm_unreachable("Can't insert epilogue before non-ret insn");

  const TMS320C64XInstrInfo &TII = *TM.getInstrInfo();

  if (TM.getSubtarget<TMS320C64XSubtarget>().enablePostRAScheduler()) {

    // unbundled epilogue instructions (weaved into other bundles)

    // restore return address (B3)
    TMS320C64XInstrInfo::addFormOp(
      TMS320C64XInstrInfo::addDefaultPred(
        BuildMI(MBB, MBBI, DL, TII.get(TMS320C64X::word_load_1))
        .addReg(TMS320C64X::B3, RegState::Define).addReg(TMS320C64X::A15)
        .addImm(-1)), TMS320C64XII::unit_d, true);

    // add the (implicit) use of B3 to ret
    MBBI->addOperand(MachineOperand::CreateReg(TMS320C64X::B3, false, true));

    // reset stack pointer
    TMS320C64XInstrInfo::addDefaultPred(
      BuildMI(MBB, MBBI, DL, TII.get(TMS320C64X::mv))
      .addReg(TMS320C64X::B15, RegState::Define).addReg(TMS320C64X::A15));

    // restore caller's frame pointer
    TMS320C64XInstrInfo::addFormOp(
      TMS320C64XInstrInfo::addDefaultPred(
        BuildMI(MBB, MBBI, DL, TII.get(TMS320C64X::word_load_1))
        .addReg(TMS320C64X::A15, RegState::Define)
        .addReg(TMS320C64X::A15).addImm(0)), TMS320C64XII::unit_d, false);
  } else {

    // epilogue via pseudo instruction (is a scheduling boundary)

    TMS320C64XInstrInfo::addDefaultPred(
      BuildMI(MBB, MBBI, DL, TII.get(TMS320C64X::epilog)));
  }
}

//-----------------------------------------------------------------------------

bool TMS320C64XFrameLowering::hasFP(const MachineFunction &MF) const {
  // Guidelines say that we should only return true if the function
  // has any variable sized arrays that get allocated on the stack,
  // so that anything else can be calculated relative to the stack
  // pointer. This is all fine, and would optimised a lot of things
  // seeing how then we wouldn't need to load stack offsets to a register
  // each time (they'd be positive).
  // However this means extra work and testing, so it's room for expansion
  // and optimisation in the future.
  return true;
}
