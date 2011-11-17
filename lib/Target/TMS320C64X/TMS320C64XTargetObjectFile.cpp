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

// There is no easy way for us to define the section printing format the TI
// assembler expects w/o changes in MC/*. So for both COFF and ELF we simply use
// .text and .data for everything.

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

const MCSection *
TMS320C64XTargetObjectFileELF:: getSectionForConstant(SectionKind Kind) const {
  // expected (and tested) for jumptables only
  assert(Kind.isReadOnly());

  return getDataSection();
}
