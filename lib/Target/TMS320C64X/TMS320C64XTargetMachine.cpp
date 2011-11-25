//===-- TMS320C64XTargetMachine.cpp - Define TargetMachine for TMS320C64X --==//
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
#include "TMS320C64XTargetMachine.h"
#include "TMS320C64XMCAsmInfo.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> EnableIfConversion("if-conversion",
  cl::Hidden, cl::desc("Perform an if-conversion for the TMS320C64X target"),
  cl::init(false));

//-----------------------------------------------------------------------------

extern "C" void LLVMInitializeTMS320C64XTarget() {
  RegisterTargetMachine<TMS320C64XTargetMachine> X(TheTMS320C64XTarget);
#if 0
  RegisterAsmInfo<TMS320C64XMCAsmInfo> Z(TheTMS320C64XTarget);
#else
  RegisterAsmInfo<TMS320C64XMCAsmInfoELF> Z(TheTMS320C64XTarget);
#endif
}

//-----------------------------------------------------------------------------

TMS320C64XTargetMachine::TMS320C64XTargetMachine(const Target &T,
                                                 const std::string &TT,
						 const std::string &FS)
: VLIWTargetMachine(T, TT),
  DataLayout("e-p:32:32:32-i8:8:8-i16:16:16-i32:32:32-f32:32:32-f64:64:64-n32"),

  // No float types - could define n40, in that the DSP supports 40 bit
  // arithmetic, however it doesn't support it for all logic operations,
  // only a variety of alu ops.
  InstrInfo(),
  Subtarget(TT, FS),
  TLInfo(*this),
  TSInfo(*this),

// NKIM, FrameInfo non-existant for llvm-2.9. The functionality is mainly
// moved to FrameLowering, also the stack-alignment params are not passed
// but hard-coded within the constructor
//  FrameInfo(TargetFrameInfo::StackGrowsDown, 8, -4)
  FrameLowering(*this),
  InstrItins(Subtarget.getInstrItineraryData())
{}

//-----------------------------------------------------------------------------

bool TMS320C64XTargetMachine::addInstSelector(PassManagerBase &PM,
                                              CodeGenOpt::Level OptLevel)
{
  PM.add(TMS320C64XCreateInstSelector(*this));
  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XTargetMachine::addPreRegAlloc(PassManagerBase &PM,
                                             CodeGenOpt::Level)
{
  if (Subtarget.enableClusterAssignment())
    PM.add(createTMS320C64XClusterAssignment(*this));
  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XTargetMachine::addPostRAScheduler(PassManagerBase &PM,
                                                CodeGenOpt::Level OptLevel)
{
  if (Subtarget.enablePostRAScheduler())
    PM.add(createTMS320C64XScheduler(*this));
  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XTargetMachine::addPreEmitPass(PassManagerBase &PM,
                                             CodeGenOpt::Level OptLevel)
{
  if (!Subtarget.enablePostRAScheduler())
    PM.add(createTMS320C64XDelaySlotFillerPass(*this));
  return true;
}

//-----------------------------------------------------------------------------

bool TMS320C64XTargetMachine::addPostISel(PassManagerBase &PM,
                                          CodeGenOpt::Level OptLevel)
{
  if (EnableIfConversion) {
    PM.add(createTMS320C64XIfConversionPass(*this));

    // NKim, makes sense to run a taildup + eventually a machine dce passes
    // afterward, due to a flatten out cfg and basic phi-elim/restructuring
    return true;
  }
  return false;
}

