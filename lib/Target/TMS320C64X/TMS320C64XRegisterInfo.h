//==- TMS320C64XRegisterInfo.h - TMS320C64X Register Information -*- C++ -*-==//
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

#ifndef LLVM_TARGET_TMS320C64X_REGISTERINFO_H
#define LLVM_TARGET_TMS320C64X_REGISTERINFO_H

#include "llvm/Target/TargetRegisterInfo.h"
#include "TMS320C64XGenRegisterInfo.h.inc"

namespace llvm {

// forwards
class TargetInstrInfo;

//class TMS320C64XRegisterInfo : TMS320C64XGenRegisterInfo {
struct TMS320C64XRegisterInfo : TMS320C64XGenRegisterInfo {

    const TargetInstrInfo &TII;

    TMS320C64XRegisterInfo(const TargetInstrInfo &tii);

    // default class dtor
    ~TMS320C64XRegisterInfo();

    const unsigned int *getCalleeSavedRegs(const MachineFunction *) const;

    const TargetRegisterClass* const *getCalleeSavedRegClasses(
                                           const MachineFunction *) const;

    BitVector getReservedRegs(const MachineFunction &MF) const;

    unsigned int getSubReg(unsigned int, unsigned int) const;

    bool requiresRegisterScavenging(const MachineFunction &MF) const;

    // NKIM, has changed for the llvm-versions higher than 2.7
    virtual void eliminateFrameIndex(MachineBasicBlock::iterator I,
                                    int SPAdj, RegScavenger *r = 0) const;

//  NKIM, moved to FrameLowering class
//    bool hasFP(const MachineFunction &MF) const;
//    void emitPrologue(MachineFunction &MF) const;
//    void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;

    /* Debug stuff, apparently */
    unsigned int getRARegister() const;
    unsigned int getFrameRegister(const MachineFunction &MF) const;
    int getDwarfRegNum(unsigned RegNum, bool isEH) const;
};

} // llvm namespace

#endif // LLVM_TARGET_TMS320C64X_REGISTERINFO_H
