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

const MCSection *TMS320C64XTargetObjectFile::
SelectSectionForGlobal(const GlobalValue *GV, SectionKind Kind,
                       Mangler *Mang, const TargetMachine &TM) const {
  // Putting a global into a section would have to be done with .usect instead
  // of .bss. But there is no "linkonce" anyway so we keep it simple.

  if (Kind.isText())
    return getTextSection();

  return getDataSection();
}
