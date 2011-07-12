//===-- TMS320C64XMachineFunctionInfo.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares the machine function info for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_MACHINEFUNCTIONINFO_H
#define LLVM_TARGET_TMS320C64X_MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/ADT/DenseMap.h"

namespace llvm {

class TMS320C64XMachineFunctionInfo : public MachineFunctionInfo {
  DenseMap<const MachineBasicBlock*, unsigned> ScheduledCycles;

public:
  TMS320C64XMachineFunctionInfo() {}

  explicit TMS320C64XMachineFunctionInfo(MachineFunction &MF) {}

  unsigned getScheduledCycles(const MachineBasicBlock *BB) const;
  bool hasScheduledCycles(const MachineBasicBlock *BB) const;
  void setScheduledCycles(const MachineBasicBlock *BB, unsigned c);
};

} // End llvm namespace

#endif
