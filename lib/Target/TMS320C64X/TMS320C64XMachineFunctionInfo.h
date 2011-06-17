//===-- llvm/ ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_MACHINEFUNCTIONINFO_H
#define LLVM_TARGET_TMS320C64X_MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class TMS320C64XMachineFunctionInfo : public MachineFunctionInfo {
  unsigned ScheduledCycles;

public:
  TMS320C64XMachineFunctionInfo() : ScheduledCycles(0) {}

  explicit TMS320C64XMachineFunctionInfo(MachineFunction &MF)
    : ScheduledCycles(0) {}

  unsigned getScheduledCycles() const { return ScheduledCycles; }
  void setScheduledCycles(unsigned c) { ScheduledCycles = c; }
};

} // End llvm namespace

#endif
