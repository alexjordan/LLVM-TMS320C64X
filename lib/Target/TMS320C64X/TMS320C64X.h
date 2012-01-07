//===-- TMS320C64X.h - Some generic definitions for TMS320C64X --*- C++ -*--==//
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

#ifndef LLVM_TARGET_TMS320C64X_TMS320C64X_H
#define LLVM_TARGET_TMS320C64X_TMS320C64X_H

#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class TMS320C64XTargetMachine;
  class PassRegistry;
  class FunctionPass;

  namespace TMS320C64X {
    /// cluster assignment algorithms available through options
    enum AssignmentAlgorithm {
      ClusterNone,
      ClusterB,
      ClusterUAS,
      ClusterUAS_none,
      ClusterUAS_rand,
      ClusterUAS_mwp
    };
  }

  // initialize the if-conversion for the TMS320C64X target
  void initializeTMS320C64XIfConversionPass(PassRegistry&);

  FunctionPass *TMS320C64XCreateInstSelector(TargetMachine &TM);
  FunctionPass *createTMS320C64XClusterAssignment(TargetMachine &tm,
                                      TMS320C64X::AssignmentAlgorithm alg);
  FunctionPass *createTMS320C64XDelaySlotFillerPass(TargetMachine &tm);
  FunctionPass *createTMS320C64XScheduler(TargetMachine &tm);
  FunctionPass *createTMS320C64XBranchDelayExpander(TargetMachine &tm);
  FunctionPass *createTMS320C64XBranchDelayReducer(TargetMachine &tm);
  FunctionPass* createTMS320C64XCallTimerPass(TMS320C64XTargetMachine &TM);

  /// createTMS320C64XIfConversionPass - create a pass for converting if/
  /// else structures for the machine basic blocks for the TMS320C64X target.
  /// This pass processes machine functions and needs to be run before RA
  FunctionPass *createTMS320C64XIfConversionPass(TMS320C64XTargetMachine &TM);

  extern Target TheTMS320C64XTarget;
}

#include "TMS320C64XGenRegisterNames.inc"
#include "TMS320C64XGenInstrNames.inc"

#endif //LLVM_TARGET_TMS320C64X_TMS320C64X_H
