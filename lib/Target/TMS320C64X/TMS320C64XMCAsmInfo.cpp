//===-- TMS320C64XTargetAsmInfo.cpp - TMS320C64X asm properties -----------===//
//
// Copyright 2010 Jeremy Morse <jeremy.morse@gmail.com>. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY JEREMY MORSE ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL JEREMY MORSE OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//===----------------------------------------------------------------------===//

#include "TMS320C64XMCAsmInfo.h"
using namespace llvm;

TMS320C64XMCAsmInfo::TMS320C64XMCAsmInfo(const Target &T, const StringRef &TT) {

  GlobalDirective = "\t.global\t";
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = 0;
  ZeroDirective = "\t.zero\t";
  AsciiDirective = "\t.string\t";
  AscizDirective = "\t.cstring\t";
  WeakRefDirective = "\t.ref ";

  CommentString = ";";
  AllowPeriodsInName = false;

  AlignmentIsInBytes = true;
  COMMDirectiveAlignmentIsInBytes = true;
  HasLCOMMDirective = false;
}

TMS320C64XMCAsmInfoELF::TMS320C64XMCAsmInfoELF(const Target &T,
                                               const StringRef &TT) {
  // AJO: we don't use custom sections, only .text and .data.
  // Also SectionDirective would have to be added to LLVM.
  //SectionDirective = "\t.sect\t";

  PrivateGlobalPrefix = "L";
  HasDotTypeDotSizeDirective = false;

  // rest same as COFF
  GlobalDirective = "\t.global\t";
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = 0;
  ZeroDirective = "\t.zero\t";

  // In TI's assembler, the .string directive does not implicitly 0-terminate
  // the string, but also does not support C escapes (\n, \t, etc.). The latter
  // is taken care of by the asm printer, which in the presence of non-
  // printable characters, emits the string as a series of .byte fields.
  AsciiDirective = "\t.string\t";

  // The behavior of cstring (0-termination and escapes handled by assembler)
  // corresponds with LLVM's AscizDirective.
  AscizDirective = "\t.cstring\t";

  WeakRefDirective = "\t.ref ";

  CommentString = ";";
  AllowPeriodsInName = false;

  AlignmentIsInBytes = true;
  COMMDirectiveAlignmentIsInBytes = true;
  HasLCOMMDirective = false;
}
