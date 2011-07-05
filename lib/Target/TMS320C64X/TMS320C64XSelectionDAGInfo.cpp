//===-- TMS320C64XSelectionDAGInfo.cpp - TI C64X SelectionDAG Info - =========//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TMS320C64XTargetSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//
//
// NOTE, even if not introducing much functionality, this class is required for
//   some cases. For example, for memcopy, memset operations, which can be lo-
//   wered in many ways (load/store sequences, target-specific implementation,
//   libcalls, etc.), the llvm-lowering routines make use of the target-specific
//   DAG information. Therefore, at least a (parent) dummy implementation is de-
//   sirable to exit.
//
// NOTE, i am not yet sure, what influence this makes on different instruction-
//   selection-strategies, such as PBQP. If it does, this may need to be changed
//   accordingly.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "tms320c64x-selectiondag-info"
#include "TMS320C64XTargetMachine.h"

using namespace llvm;

//-----------------------------------------------------------------------------

TMS320C64XSelectionDAGInfo::
TMS320C64XSelectionDAGInfo(const TMS320C64XTargetMachine &TM)
: TargetSelectionDAGInfo(TM)
{
  // empty ctor
}

//-----------------------------------------------------------------------------

TMS320C64XSelectionDAGInfo::~TMS320C64XSelectionDAGInfo() {
  // empty dtor
}
