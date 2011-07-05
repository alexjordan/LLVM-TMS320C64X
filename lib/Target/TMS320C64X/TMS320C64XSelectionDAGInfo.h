//===-- TMS320C64XSelectionDAGInfo.h - TI C64X SelectionDAG Info -- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TMS320C64X subclass for TargetSelectionDAGInfo.
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

#ifndef TMS320C64XSELECTIONDAGINFO_H
#define TMS320C64XSELECTIONDAGINFO_H

#include "llvm/Target/TargetSelectionDAGInfo.h"

namespace llvm {

class TMS320C64XTargetMachine;

class TMS320C64XSelectionDAGInfo : public TargetSelectionDAGInfo {

public:

  explicit TMS320C64XSelectionDAGInfo(const TMS320C64XTargetMachine &TM);
  ~TMS320C64XSelectionDAGInfo();
};
}

#endif

