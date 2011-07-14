//===- TMS320C64XMBBSpecializer.cpp - TMS320C64X MBB overrides --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This set of functions defines target specific handling of MachineBasicBlocks.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/CodeGen/MachineBasicBlock.h"

using namespace llvm;

class TMS320C64XMBBSpecializer : public MBBSpecializer {
public:
  virtual MachineBasicBlock::iterator
    findFirstTerminator(MachineBasicBlock *MBB);

};

MachineBasicBlock::iterator
TMS320C64XMBBSpecializer::findFirstTerminator(MachineBasicBlock *MBB) {
  MachineBasicBlock::iterator I = MBB->begin(),
                              E = MBB->end();
  for (; I != E; ++I) {
    // some instructions have isTerminator set but do not actually terminate
    // a block (read: legacy)
    switch (I->getOpcode()) {
      case TMS320C64X::prolog:
      case TMS320C64X::epilog:
        continue;
    }

    // stop when hitting a terminator
    if (I->getDesc().isTerminator())
      break;
  }
  assert(I == MBB->end() || I->getDesc().isTerminator());
  return I;
}

extern "C" void InitMBBSpecializer() {
    RegisterMBBSpecializer<TMS320C64XMBBSpecializer> X(TheTMS320C64XTarget);
}

