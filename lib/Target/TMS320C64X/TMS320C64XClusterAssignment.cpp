//===-- TMS320C64XClusterAssignment.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Cluster assignment for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64XClusterAssignment.h"

using namespace llvm;

char TMS320C64XClusterAssignment::ID = 0;

FunctionPass *llvm::createTMS320C64XClusterAssignment(TargetMachine &tm) {
  return new TMS320C64XClusterAssignment(tm);
}
