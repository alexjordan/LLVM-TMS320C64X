//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TMS320C64XTargetObjectFile.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

// We can't properly define sections, neither in COFF nor in ELF. So for both
// formats we simply use .text and .data for everything.

const MCSection *TMS320C64XTargetObjectFile::
SelectSectionForGlobal(const GlobalValue *GV, SectionKind Kind,
                       Mangler *Mang, const TargetMachine &TM) const {
  if (Kind.isText())
    return getTextSection();

  return getDataSection();
}

const MCSection *TMS320C64XTargetObjectFileELF::
SelectSectionForGlobal(const GlobalValue *GV, SectionKind Kind,
                       Mangler *Mang, const TargetMachine &TM) const {
  if (Kind.isText())
    return getTextSection();

  return getDataSection();
}
