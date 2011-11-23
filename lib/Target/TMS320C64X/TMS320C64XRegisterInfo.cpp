//===- TMS320C64XRegisterInfo.cpp - TMS320C64X Register Information -------===//
//
// Copyright 2010 Jeremy Morse <jeremy.morse@gmail.com>. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY JEREMY MORSE ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL JEREMY MORSE OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "TMS320C64XRegisterInfo.h"
#include "TMS320C64XSubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"

// include generated register description
#include "TMS320C64XGenRegisterInfo.inc"

// NKIM, bad, circular, but ok for now
#include "TMS320C64XInstrInfo.h"

// Actual register information
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

//-----------------------------------------------------------------------------

TMS320C64XRegisterInfo::TMS320C64XRegisterInfo(const TargetInstrInfo &tii)
: TMS320C64XGenRegisterInfo(),
  TII(tii)
{}

//-----------------------------------------------------------------------------

TMS320C64XRegisterInfo::~TMS320C64XRegisterInfo() {}

//-----------------------------------------------------------------------------

BitVector
TMS320C64XRegisterInfo::getReservedRegs(const MachineFunction &MF) const {

  BitVector Reserved(getNumRegs());
  Reserved.set(TMS320C64X::B15);
  Reserved.set(TMS320C64X::A15);
  Reserved.set(TMS320C64X::A14);
  return Reserved;
}

//-----------------------------------------------------------------------------

const unsigned *
TMS320C64XRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {

  static const unsigned nonvolatileRegs[] = {
    TMS320C64X::A10,
    TMS320C64X::A11,
    TMS320C64X::A12,
    TMS320C64X::A13,
    TMS320C64X::B10,
    TMS320C64X::B11,
    TMS320C64X::B12,
    TMS320C64X::B13,
    0
  };

  return nonvolatileRegs;
}

//-----------------------------------------------------------------------------

const TargetRegisterClass* const*
TMS320C64XRegisterInfo::getCalleeSavedRegClasses(MachineFunction const*) const {
  // XXX not sure about the reg classes here...
  static const TargetRegisterClass *const calleeNonvolatileRegClasses[] ={
    &TMS320C64X::GPRegsRegClass, &TMS320C64X::GPRegsRegClass,
    &TMS320C64X::GPRegsRegClass, &TMS320C64X::GPRegsRegClass,
    &TMS320C64X::BRegsRegClass, &TMS320C64X::BRegsRegClass,
    &TMS320C64X::BRegsRegClass, &TMS320C64X::BRegsRegClass
  };

  return calleeNonvolatileRegClasses;
}

//-----------------------------------------------------------------------------

unsigned int
TMS320C64XRegisterInfo::getSubReg(unsigned int, unsigned int) const {
  llvm_unreachable("Unimplemented function getSubReg\n");
}

//-----------------------------------------------------------------------------

bool
TMS320C64XRegisterInfo::requiresRegisterScavenging(const MachineFunction&) const {
  return true;
}

//-----------------------------------------------------------------------------

void
TMS320C64XRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MBBI,
                                            int SPAdj, RegScavenger *r) const
{
  using namespace TMS320C64X;

  unsigned i, frame_index, reg, access_alignment;
  int offs;

  /* XXX - Value turned up in 2.7, I don't know what it does. */

  MachineInstr &MI = *MBBI;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineBasicBlock &MBB = *MI.getParent();

//  DebugLoc dl = DebugLoc::getUnknownLoc();

  DebugLoc dl;
  if (MBBI != MBB.end()) dl = MI.getDebugLoc();

  i = 0;

  // TODO, a weak break-condition !
  while (!MI.getOperand(i).isFI()) ++i;

  assert(i < MI.getNumOperands() && "No FrameIndex in eliminateFrameIdx");

  frame_index = MI.getOperand(i).getIndex();
  offs = MF.getFrameInfo()->getObjectOffset(frame_index);

  const TargetInstrDesc tid = MI.getDesc();
  access_alignment = (tid.TSFlags & TMS320C64XII::mem_align_amt_mask)
		      >> TMS320C64XII::mem_align_amt_shift;

  // Firstly, is this actually memory access? Might be lea (vomit)
  if (!(MI.getDesc().TSFlags & TMS320C64XII::is_memaccess)) {
    // If so, the candidates are sub and add - each of which
    // have an sconst5 range. If the offset doesn't fit in there,
    // need to scavenge a register
    if (TMS320C64XInstrInfo::check_sconst_fits(offs, 5)) {
      MI.getOperand(i).ChangeToImmediate(offs);
      return;
    }

    access_alignment = 0;

    // So for memory, will this frame index actually fit inside the
    // instruction field?
  }
  else if (TMS320C64XInstrInfo::check_uconst_fits(abs(offs),
                                      5 + access_alignment))
  {
    // Constant fits into instruction but needs to be scaled.

    MI.getOperand(i).ChangeToImmediate(offs >> access_alignment);
    return;
  }

  // Otherwise, we need to do some juggling to load that constant into
  // a register correctly. First of all, because of the highly-unpleasent
  // scaling feature of using indexing instructions we need to shift
  // the stack offset :|
  if (offs & ((1 << access_alignment) -1 ))
    llvm_unreachable("Unaligned stack access - should never occur");

  offs >>= access_alignment;

  const TargetRegisterClass *c;

  if (tid.TSFlags & TMS320C64XII::is_bside)
    c = TMS320C64X::BRegsRegisterClass;
  else c = TMS320C64X::ARegsRegisterClass;

  reg = r->FindUnusedReg(c);

  if (reg == 0) {
    // XXX - this kicks a register out and lets us use it but...
    // that'll lead to a store, to a stack slot, which will mean
    // this method is called again. Explosions?
    reg = r->scavengeRegister(c, MBBI, 0);
  }

  unsigned side_a = (c == TMS320C64X::ARegsRegisterClass);

  if (TMS320C64XInstrInfo::check_sconst_fits(offs, 16)) {
    // fits into one mvk
    TMS320C64XInstrInfo::addFormOp(
      TMS320C64XInstrInfo::addDefaultPred(
        BuildMI(MBB, MBBI, dl, TII.get(side_a ? mvk_1 : mvk_2))
          .addReg(reg, RegState::Define).addImm(offs)),
            TMS320C64XII::unit_s, false);
  } else {
    // needs a mvkh/mvkl pair
    TMS320C64XInstrInfo::addFormOp(
      TMS320C64XInstrInfo::addDefaultPred(
        BuildMI(MBB, MBBI, dl, TII.get(side_a ? mvkl_1 : mvkl_2))
          .addReg(reg, RegState::Define).addImm(offs)),
            TMS320C64XII::unit_s, false);
    TMS320C64XInstrInfo::addFormOp(
      TMS320C64XInstrInfo::addDefaultPred(
        BuildMI(MBB, MBBI, dl, TII.get(side_a ? mvkh_1 : mvkh_2))
          .addReg(reg, RegState::Define).addImm(offs)
            .addReg(reg)),
              TMS320C64XII::unit_s, false);
  }

  MI.getOperand(i).ChangeToRegister(reg, false, false, true);
}

//-----------------------------------------------------------------------------

int
TMS320C64XRegisterInfo::getDwarfRegNum(unsigned reg_num, bool isEH) const {
  llvm_unreachable("Unimplemented function getDwarfRegNum\n");
}

//-----------------------------------------------------------------------------

unsigned int
TMS320C64XRegisterInfo::getRARegister() const {
  llvm_unreachable("Unimplemented function getRARegister\n");
}

//-----------------------------------------------------------------------------

unsigned int
TMS320C64XRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  llvm_unreachable("Unimplemented function getFrameRegister\n");
}
