//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TMS320C64X_TARGETOBJECTFILE_H
#define LLVM_TARGET_TMS320C64X_TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

  class TMS320C64XTargetObjectFile : public TargetLoweringObjectFileCOFF {
    const MCSection *SmallDataSection;
    const MCSection *SmallBSSSection;
  public:

    const MCSection *SelectSectionForGlobal(const GlobalValue *GV,
                                            SectionKind Kind,
                                            Mangler *Mang,
                                            const TargetMachine &TM) const;

  };

  class TMS320C64XTargetObjectFileELF : public TargetLoweringObjectFileELF {
  public:
    const MCSection *SelectSectionForGlobal(const GlobalValue *GV,
                                            SectionKind Kind,
                                            Mangler *Mang,
                                            const TargetMachine &TM) const;
  };
} // end namespace llvm

#endif
