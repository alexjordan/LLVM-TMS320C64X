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

#ifndef LLVM_TARGET_TMS320C64X_FRAMELOWERINGINFO_H
#define LLVM_TARGET_TMS320C64X_FRAMELOWERINGINFO_H

#include "TMS320C64X.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {

class TMS320C64XFrameLowering : public TargetFrameLowering {
    const TMS320C64XTargetMachine &TM;

  public:

   explicit TMS320C64XFrameLowering(const TMS320C64XTargetMachine &tm)
   : TargetFrameLowering(StackGrowsDown, 8, -4),
     TM(tm)
   {}

    // NKIM, moved from InstInfo to the FrameLowering class for llvm 2.9
    virtual bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const std::vector<CalleeSavedInfo> &,
                                           const TargetRegisterInfo*) const;

    // pure virtual methods need to be implemented
    void emitPrologue(MachineFunction &MF) const;
    void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;
    bool hasFP(const MachineFunction &MF) const;
};

} // llvm namespace

#endif // LLVM_TARGET_TMS320C64X_FRAMELOWERINGINFO_H
