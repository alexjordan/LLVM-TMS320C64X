//===-- TMS320C64XMachineFunctionInfo.cpp -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the machine function info for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64XMachineFunctionInfo.h"

using namespace llvm;

unsigned
TMS320C64XMachineFunctionInfo::getScheduledCycles(
    const MachineBasicBlock *BB) const {
  assert(ScheduledCycles.find(BB) != ScheduledCycles.end());
  return ScheduledCycles.lookup(BB);
}

bool
TMS320C64XMachineFunctionInfo::hasScheduledCycles(
    const MachineBasicBlock *BB) const {
  return ScheduledCycles.find(BB) != ScheduledCycles.end();
}

void
TMS320C64XMachineFunctionInfo::setScheduledCycles(const MachineBasicBlock *BB,
    unsigned c) {
  assert(ScheduledCycles.find(BB) == ScheduledCycles.end());
  ScheduledCycles[BB] = c;
}
