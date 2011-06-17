//==- TMS320C64XTargetMachine.h - Define TargetMachine for tic64x *- C++ -*-==//
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

#ifndef LLVM_TARGET_TMS320C64X_TARGETMACHINE_H
#define LLVM_TARGET_TMS320C64X_TARGETMACHINE_H

#include "TMS320C64XInstrInfo.h"
#include "TMS320C64XLowering.h"
#include "TMS320C64XFrameLowering.h"
#include "TMS320C64XHazardRecognizer.h"
#include "TMS320C64XSubtarget.h"

#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetData.h"

namespace llvm {

class TMS320C64XTargetMachine : public LLVMTargetMachine {

    const TargetData		DataLayout;
    TMS320C64XInstrInfo		InstrInfo;
    TMS320C64XSubtarget		Subtarget;
    TMS320C64XLowering		TLInfo;
    TMS320C64XFrameLowering     FrameLowering;
    InstrItineraryData          InstrItins;

  public:

    TMS320C64XTargetMachine(const Target &T,
                            const std::string &TT,
			    const std::string &FS);

    virtual const TargetFrameLowering *getFrameLowering() const {
      return &FrameLowering;
    }

    virtual const TMS320C64XInstrInfo *getInstrInfo() const {
      return &InstrInfo;
    }

    virtual const TargetData *getTargetData() const {
      return &DataLayout;
    }

    virtual const TMS320C64XSubtarget *getSubtargetImpl() const {
      return &Subtarget;
    }

    virtual const TMS320C64XRegisterInfo *getRegisterInfo() const {
      return &InstrInfo.getRegisterInfo();
    }

    virtual TMS320C64XLowering *getTargetLowering() const {
      return const_cast<TMS320C64XLowering*>(&TLInfo);
    }

    virtual const InstrItineraryData *getInstrItineraryData() const {
      return &InstrItins;
    }

    virtual bool getEnableTailMergeDefault() const {
      return false;
    }

    virtual bool addInstSelector(PassManagerBase &PM,
				 CodeGenOpt::Level OptLevel);

    virtual bool addPostRAScheduler(PassManagerBase &PM,
                                    CodeGenOpt::Level OptLevel);

    virtual bool addPreEmitPass(PassManagerBase &PM,
				CodeGenOpt::Level OptLevel);

    // Don't wish to overcomplicate things right now
};

} // namespace llvm

#endif // LLVM_TARGET_TMS320C64X_TARGETMACHINE_H
