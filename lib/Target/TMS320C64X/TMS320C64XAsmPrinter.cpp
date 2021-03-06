//===-- TMS320C64XAsmPrinter.cpp - TMS320C64X LLVM assembly writer --------===//
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

#define DEBUG_TYPE "amsprinter"
#include "TMS320C64X.h"
#include "TMS320C64XInstrInfo.h"
#include "TMS320C64XRegisterInfo.h"
#include "TMS320C64XSubtarget.h"
#include "TMS320C64XTargetMachine.h"
#include "TMS320C64XMachineFunctionInfo.h"
#include "TMS320C64XMCAsmInfo.h"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"

using namespace llvm;

namespace {

    typedef MachineBasicBlock::const_iterator MIiter;
    typedef std::pair<MIiter, MIiter> MIRange;

class TMS320C64XAsmPrinter : public AsmPrinter {

    SmallVector<const char *, 4> UnitStrings;
    const TMS320C64XSubtarget &ST;
    bool BundleMode;

  public:
    explicit TMS320C64XAsmPrinter(TargetMachine &TM, MCStreamer &MCS);

    virtual const char *getPassName() const {
      return "TMS320C64X Assembly Printer";
    }

    const char *getRegisterName(unsigned RegNo);

    bool handleSoftFloatCall(const char* SymbolName);
    void refSymbol(MCSymbol *Sym);
    void refContents(Constant *C);

    bool print_predicate(const MachineInstr *MI,
                         raw_ostream &OS,
                         const char *prefix = "\t");

    void emit_prolog(const MachineInstr *MI);
    void emit_epilog(const MachineInstr *MI);

    // if MI is special, emit it and return true
    bool emit_special(const MachineInstr *MI);

    // the following methods return an iter to the MI they handled last
    MIiter emit_instructions(MIRange mir);
    MIiter emit_bundle(MIRange mir);

    bool runOnMachineFunction(MachineFunction &F);

    virtual void EmitGlobalVariable(const GlobalVariable *GVar);

    /// NKIM, signatures changed for llvm-versions higher than 2.8

    void printFU(const MachineInstr *MI, int opNum, raw_ostream &O);

    void printOperand(const MachineInstr *MI, int opNum, raw_ostream &O);

    void printMemOperand(const MachineInstr *MI, int opNum, raw_ostream &O);

    void printInstruction(const MachineInstr *MI, raw_ostream &O);

    void printCCOperand(const MachineInstr *MI, int opNum);

    bool PrintAsmOperand(const MachineInstr *MI,
                         unsigned OperandNumber,
                         unsigned AsmVariant,
                         const char *ExtraCode,
                         raw_ostream &outputStream);

    bool PrintAsmMemoryOperand(const MachineInstr *MI,
                               unsigned OperandNumber,
                               unsigned AsmVariant,
                               const char *ExtraCode,
                               raw_ostream &outputStream);

    virtual void EmitFunctionBodyStart();
    virtual void EmitStartOfAsmFile(Module &);
    virtual void EmitEndOfAsmFile(Module &M);

    void printMBBInfo(const MachineBasicBlock *MBB);
};

} // anonymous

#include "TMS320C64XGenAsmWriter.inc"

//-----------------------------------------------------------------------------

TMS320C64XAsmPrinter::TMS320C64XAsmPrinter(TargetMachine &TM, MCStreamer &MCS)
: AsmPrinter(TM, MCS),
  UnitStrings(TMS320C64XInstrInfo::getUnitStrings()),
  ST(TM.getSubtarget<TMS320C64XSubtarget>()),
  BundleMode(ST.enablePostRAScheduler())
{}

//-----------------------------------------------------------------------------

bool TMS320C64XAsmPrinter::runOnMachineFunction(MachineFunction &MF) {

  const Function *F = MF.getFunction();
  this->MF = &MF;
  Twine NewLine("\n");

  SetupMachineFunction(MF);
  EmitConstantPool();

  OutStreamer.EmitRawText(StringRef("\n\n"));
  EmitAlignment(F->getAlignment(), F);

  EmitFunctionBodyStart();
  EmitFunctionHeader();

  // Due to having to beat predecates manually, we don't use
  // EmitFunctionBody, but instead pump out instructions manually

  MachineFunction::const_iterator MBB;
  for (MBB = MF.begin(); MBB != MF.end(); ++MBB) {

    // print block info right before the block
    OutStreamer.EmitRawText(NewLine);
    printMBBInfo(MBB);

    if (MBB != MF.begin()) {
      EmitBasicBlockStart(MBB);
      OutStreamer.EmitRawText(NewLine);
    }


    for (MachineBasicBlock::const_iterator MI = MBB->begin(), ME = MBB->end();
         MI != ME; ++MI) {
      switch (MI->getDesc().getOpcode()) {
        case TMS320C64X::prolog: emit_prolog(MI); break;
        case TMS320C64X::epilog: emit_epilog(MI); break;
        default:
          MI = emit_instructions(std::make_pair(MI, MBB->end()));
          break;
      }
    }
  }

  // Print out jump tables referenced by the function.
  EmitJumpTableInfo();

  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XAsmPrinter::handleSoftFloatCall(const char *SymbolName) {

  if (ST.hasLibcall(SymbolName)) {
    // don't mangle FP calls, but add to .refs
    refSymbol(OutContext.GetOrCreateSymbol(StringRef(SymbolName)));
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::emit_prolog(const MachineInstr *MI) {

  // See instr info td file for why we do this here

  SmallString<256> prologueString;
  raw_svector_ostream OS(prologueString);

  // If the stack offset is larger than 16-bit (signed), the prologue needs to
  // include an mvkl/mvkh pair.
  bool smallStack =
    TMS320C64XInstrInfo::check_sconst_fits(MI->getOperand(0).getImm(), 16);

  OS << "\t; begin prolog\n";

  if (smallStack) {
    OS << "\t\tmvk\t\t";
    printOperand(MI, 0, OS);
    OS << ",\tA0\n";
  } else {
    OS << "\t\tmvkl\t\t";
    printOperand(MI, 0, OS);
    OS << ",\tA0\n";
  }

  OS << "\t||\tmv\t\tB15,\tA1\n";

  if (!smallStack) {
    OS << "\t\tmvkh\t\t";
    printOperand(MI, 0, OS);
    OS << ",\tA0\n";
  }

  OS << "\t\tstw\t\tA15,\t*B15\n";
  OS << "\t||\tstw\t\tB3,\t*-A1(4)\n";
  OS << "\t||\tmv\t\tB15,\tA15\n";
  OS << "\t||\tsub\t\tB15,\tA0\t,B15\n";
  OS << "\t; end prolog\n";

  OutStreamer.EmitRawText(OS.str());
  return;
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::emit_epilog(const MachineInstr *MI) {

  // See instr info td file for why we do this here

  SmallString<256> epilogueString;
  raw_svector_ostream OS(epilogueString);

  OS << "\n";
  OS << "\t; begin epilog\n";
  OS << "\t\tldw\t\t*-A15(4),\tB3\n";
  OS << "\t\tmv\t\tA15,\tB15\n";
  OS << "\t||\tldw\t\t*A15,\tA15\n";
  OS << "\t\tnop\t\t4\n";
  OS << "\t; end epilog\n";

  OutStreamer.EmitRawText(OS.str());
  return;
}

//-----------------------------------------------------------------------------

MIiter TMS320C64XAsmPrinter::emit_instructions(MIRange mir) {
  const MachineInstr *MI = mir.first;

  // emit special pseudo instructions (labels) regardless of bundling
  if (emit_special(mir.first))
    return mir.first;

  // if code is bundled, emit a bundle at a time
  if (BundleMode)
    return emit_bundle(mir);

  // emit a single (ordinary) instruction
  SmallString<64> str;
  raw_svector_ostream OS(str);

  print_predicate(MI, OS);
  printInstruction(MI, OS);

  OS << "\n";
  OutStreamer.EmitRawText(OS.str());
  return mir.first;
}

MIiter TMS320C64XAsmPrinter::emit_bundle(MIRange mir) {
  SmallString<512> bundleString;
  raw_svector_ostream OS(bundleString);

  unsigned bundleSize = 0;
  bool nopBundle = false;

  MIiter I = mir.first;
  for (; I != mir.second; ++I) {
    const MachineInstr *MI = I;
    const char *prefix = "\t";

    // a bunch of nops will be spaced similar to a bundle
    if (MI->getDesc().getOpcode() == TMS320C64X::noop)
      nopBundle = true;

    // bundle ends are skipped when there is another nop ahead
    if (MI->getDesc().getOpcode() == TMS320C64X::BUNDLE_END) {
      MIiter next = llvm::next(I);
      if (nopBundle && next != mir.second &&
          next->getDesc().getOpcode() == TMS320C64X::noop)
        continue;
      else
        break; // end the current bundle
    }

    // this instructions marks the end of the delay slots following a branch.
    // the branch acutally happened in the cycle before, thus we print the
    // informational output before the contents of the current bundle.
    if (MI->getDesc().getOpcode() == TMS320C64X::BR_OCCURS) {
      SmallString<512> brStr;
      raw_svector_ostream OS(brStr);
      print_predicate(MI, OS);
      printInstruction(MI, OS);
      OS << "\n\n";
      OutStreamer.EmitRawText(OS.str());
      continue;
    }

    // continue within bundle
    if (bundleSize && !nopBundle)
      prefix = "\t||";

    print_predicate(MI, OS, prefix);
    printInstruction(MI, OS);
    bundleSize++;
    OS << "\n";
  }
  assert(bundleSize <= 8);
  if (bundleSize) {
    OS << "\n";
    OutStreamer.EmitRawText(OS.str());
  }
  return I;
}

bool TMS320C64XAsmPrinter::emit_special(const MachineInstr *MI) {
  SmallString<64> str;
  raw_svector_ostream OS(str);
  switch (MI->getDesc().getOpcode()) {
    case TargetOpcode::INLINEASM:
      OS << MI->getOperand(0).getSymbolName();
      break;

    case TMS320C64X::call_return_label:
      // instead of calling printInstruction we emit the label directly,
      // this allows us to avoid tabs being inserted automatically
      assert(MI->getOperand(0).isSymbol() && "Bad symbol operand!");
      OS << "\n" << MI->getOperand(0).getSymbolName() << ":\t\t"
         << MAI->getCommentString() << " return label for reg-calls\n";
      break;

    default:
      return false; // nothing to emit
  }

  OS << "\n";
  OutStreamer.EmitRawText(OS.str());
  return true;
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::printFU(const MachineInstr *MI,
                                   int opNum,
                                   raw_ostream &OS)
{
  int fuOp = MI->getOperand(opNum).getImm();

  OS << UnitStrings[fuOp >> 1];
  OS << (IS_BSIDE(MI->getDesc().TSFlags) ? "2" : "1");

  // append datapath for load/stores
  if (MI->getDesc().TSFlags & TMS320C64XII::is_memaccess) {
    // we don't always get the dataline right in single side mode
    if (ST.enablePostRAScheduler()) {
      if (fuOp & 0x1) OS << "T2";
      else OS << "T1";
    }
    return;
  }
  // append XPath otherwise
  if (fuOp & 0x1) OS << "X";
}

//-----------------------------------------------------------------------------

bool TMS320C64XAsmPrinter::print_predicate(const MachineInstr *MI,
                                           raw_ostream &OS,
                                           const char *prefix)
{
  const TargetRegisterInfo &RI = *TM.getRegisterInfo();

  // Can't use first predicate operand any more, due to unit_operand hack
  int pred_idx = MI->findFirstPredOperandIdx();

  if (!MI->getDesc().isPredicable()) return false;

  if (pred_idx == -1) {
    // No predicate here
    OS << prefix;
    return false;
  }

  int nz = MI->getOperand(pred_idx).getImm();
  int reg = MI->getOperand(pred_idx+1).getReg();

  if (nz == -1) {
    // This isn't a predicate
    OS << prefix;
    return false;
  }

  char c = nz ? ' ' : '!';

  if (!TargetRegisterInfo::isPhysicalRegister(reg))
    llvm_unreachable("Nonphysical register used for predicate");

  OS << prefix << "[" << c << RI.getName(reg) << "]";

  return true;
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::EmitGlobalVariable(const GlobalVariable *GVar) {

  // Comments elsewhere say we discard this because external globals
  // require no code; why do we have to do that here though?
  if (!GVar->hasInitializer()) return;

  if (EmitSpecialLLVMGlobal(GVar)) return;

  OutStreamer.EmitRawText(StringRef("\n\n"));

  SmallString<60> NameStr;
  Mang->getNameWithPrefix(NameStr, GVar, false);
  Constant *C = GVar->getInitializer();

  const TargetData *td = TM.getTargetData();
  unsigned sz = td->getTypeAllocSize(C->getType());
  unsigned align = td->getPreferredAlignment(GVar);

  OutStreamer.SwitchSection(getObjFileLowering()
    .SectionForGlobal(GVar, Mang, TM));

  SmallString<1024> globalString;
  raw_svector_ostream OS(globalString);

  if (C->isNullValue() && !GVar->hasSection()) {
    if (!GVar->isThreadLocal() &&
        (GVar->hasLocalLinkage() || GVar->isWeakForLinker()))
    {
      if (sz == 0) sz = 1;

      // XXX - .lcomm?
      OS << "\t.bss\t" << NameStr
         << "," << sz << ", " << align;

      OutStreamer.EmitRawText(OS.str());
      return;
    }
  }

  // prepend .global directive
  if (GVar->hasExternalLinkage()) {
    OS << MAI->getGlobalDirective() << NameStr << "\n";
  }

  const ConstantArray *CVA = dyn_cast<ConstantArray>(C);
  const bool isCharString = CVA && CVA->isString();


  // TI assembler does not support this (remains here for reference)
  //if (MAI->hasDotTypeDotSizeDirective()) {
  //  OS << "\t.type " << NameStr << ",#object\n";
  //  OS << "\t.size " << NameStr << ',' << sz << '\n';
  //}

  OS << NameStr << ":\n";

  /// NKim, handle the emission of character arrays aggregated into a string
  /// manually here.
  if (isCharString) {
    bool stringIsPrintable = true;

    // check whether the string contains any non-printable characters
    // XXX could be improved by not falling back to the .byte sequence when
    // .cstring (which allows for C escapes) is used.
    for (unsigned i = 0, e = CVA->getNumOperands(); i != e; ++i) {
      char charVal = cast<ConstantInt>(CVA->getOperand(i))->getZExtValue();
      stringIsPrintable = stringIsPrintable && (isprint(charVal) || !charVal);
    }

    // if the string contains any non printable characters, we emit it as a
    // sequential collection of bytes, since a simple .string/.cstring emis-
    // sion does seem to work currently for our tool-chain
    if (!stringIsPrintable) {
      for (unsigned j = 0, e = CVA->getNumOperands(); j != e; ++j) {
        char charVal = cast<ConstantInt>(CVA->getOperand(j))->getZExtValue();
        OS << MAI->getData8bitsDirective() << (int) charVal << '\t';

        if (std::isprint(charVal))
          OS << MAI->getCommentString() << " '" << charVal << "'\n";
        else OS << MAI->getCommentString() << " escape\n";
      }

      OutStreamer.EmitRawText(OS.str());
      refContents(C);
      return;
    }
  }

  OutStreamer.EmitRawText(OS.str());
  EmitGlobalConstant(C);
  refContents(C);
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::printOperand(const MachineInstr *MI,
                                        int op_num,
                                        raw_ostream &OS)
{
  SmallString<60> NameStr;
  MCSymbol *sym;

  const MachineOperand &MO = MI->getOperand(op_num);
  const TargetRegisterInfo &RI = *TM.getRegisterInfo();

  switch(MO.getTargetFlags()) {
    case 0: break; /* no flags */
    case 1:
      DEBUG(dbgs() << "Target flag detected: "
            << (int) MO.getTargetFlags() << "\n");
      break;
    default: llvm_unreachable("unknown target flag");
  }


  switch(MO.getType()) {
    case MachineOperand::MO_Register:
      if (TargetRegisterInfo::isPhysicalRegister(MO.getReg()))
        OS << RI.getName(MO.getReg());
      else llvm_unreachable("Nonphysical register being printed");
      break;

    case MachineOperand::MO_Immediate:
      OS << (int)MO.getImm();
      break;

    case MachineOperand::MO_MachineBasicBlock:
      sym = MO.getMBB()->getSymbol();
      OS << (*sym);
      break;

    case MachineOperand::MO_GlobalAddress:
      Mang->getNameWithPrefix(NameStr, MO.getGlobal(), false);
      OS << NameStr;
      // if GV is an external symbol, it needs a .ref
      if (MO.getGlobal()->isDeclaration())
        refSymbol(Mang->getSymbol(MO.getGlobal()));
      break;

    case MachineOperand::MO_ExternalSymbol:
      if (MO.getTargetFlags() ||
          handleSoftFloatCall(MO.getSymbolName())) {
        // the target flag is set when this is a local label lowered as an
        // external symbol, leave the symbol/label name as is.
        // (also for an already mangled call to a softfloat function)
        OS << MO.getSymbolName();
      } else {
        // symbol name needs mangling
        Mang->getNameWithPrefix(NameStr, MO.getSymbolName());
        OS << NameStr;
        refSymbol(OutContext.GetOrCreateSymbol(StringRef(NameStr)));
      }
      break;

    case MachineOperand::MO_JumpTableIndex:
      OS << MAI->getPrivateGlobalPrefix() << "JTI" << getFunctionNumber()
        << '_' << MO.getIndex();
      break;

    case MachineOperand::MO_ConstantPoolIndex:
    default:
      llvm_unreachable("Unknown operand type");
  }
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::printMemOperand(const MachineInstr *MI,
                                           int op_num,
                                           raw_ostream &OS)
{
  int offset = 0;

  if (MI->getDesc().getOpcode() == TMS320C64X::lea_fail) {
    // I can't find a reasonable way to bounce a memory addr
    // calculation into normal operands (1 -> 2), so hack
    // this instead
    printOperand(MI, op_num, OS);

    OS << "," << '\t';

    printOperand(MI, op_num+1, OS);
    return;
  }

  OS << "*";

  // We may need to put a + or - in front of the base register to indicate
  // what we plan on doing with the constant
  if (MI->getOperand(op_num+1).isImm()) {
    offset = MI->getOperand(op_num+1).getImm();

    if (offset < 0) OS << "-";
    else if (offset > 0) OS << "+";
  }

  // Base register
  printOperand(MI, op_num, OS);

  // Don't print zero offset, and if it's an immediate always print
  // a positive offset. Offset is expected to be scaled, use '[]',
  // never use non-scaled braces '()'.
  if (MI->getOperand(op_num+1).isImm()) {
    if (offset != 0) {
      OS << "[" << abs(offset) << "]";
    }
  }
  else {
    OS << "[";
    printOperand(MI, op_num+1, OS);
    OS << "]";
  }

  return;
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::printCCOperand(const MachineInstr *MI, int opNum) {
  llvm_unreachable_internal("Unimplemented function printCCOperand");
}

//-----------------------------------------------------------------------------

bool TMS320C64XAsmPrinter::PrintAsmOperand(const MachineInstr *MI,
                                           unsigned OpNo,
                                           unsigned AsmVariant,
                                           const char *ExtraCode,
                                           raw_ostream &outputStream)
{
  // why this ?
  llvm_unreachable_internal("Unimplemented function PrintAsmOperand");
}

//-----------------------------------------------------------------------------

bool TMS320C64XAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                                 unsigned OpNo,
                                                 unsigned AsmVariant,
                                                 const char *ExtraCode,
                                                 raw_ostream &outputStream)
{
  // why this ?
  llvm_unreachable_internal("Unimplemented func PrintAsmMemoryOperand");
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::refSymbol(MCSymbol *MCSym) {
  // we use the obj file info for mach to store externals refs
  MachineModuleInfoMachO &MMIMachO =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();

  // skip anything that is defined in the module
  if (MCSym->isDefined())
    return;

  MachineModuleInfoImpl::StubValueTy &StubSym = MMIMachO.getFnStubEntry(MCSym);
  if (StubSym.getPointer() == 0)
    StubSym = MachineModuleInfoImpl::StubValueTy(MCSym, false);
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::refContents(Constant *C) {
  for (unsigned i = 0, e = C->getNumOperands(); i != e; ++i) {
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(C->getOperand(i)))
    if (GV->isDeclaration())
      refSymbol(Mang->getSymbol(GV));
  }
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::EmitStartOfAsmFile(Module &) {

  SmallString<256> str;
  raw_svector_ostream OS(str);

  // build the magic compiler options line
  OS << "\t.compiler_opts ";
  OS << ST.getABIOptionString() << " ";
  OS << "--c64p_l1d_workaround=default --endian=little ";
  OS << "--hll_source=on --silicon_version=6500 ";
  OS << "\n";

  OutStreamer.EmitRawText(OS.str());
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::EmitEndOfAsmFile(Module &M) {
  MachineModuleInfoMachO &MMIMacho =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();
  MachineModuleInfoMachO::SymbolListTy Stubs = MMIMacho.GetFnStubList();
  if (!Stubs.empty())
    OutStreamer.AddBlankLine();

  // emit as .ref
  for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
    OutStreamer.EmitSymbolAttribute(Stubs[i].first, MCSA_WeakReference);
  }
}

//-----------------------------------------------------------------------------

void
TMS320C64XAsmPrinter::printMBBInfo(const MachineBasicBlock *MBB) {

  const TMS320C64XMachineFunctionInfo *MFI =
    MF->getInfo<TMS320C64XMachineFunctionInfo>();

  if (!MFI->hasScheduledCycles(MBB))
    return;

  SmallString<128> str;
  raw_svector_ostream OS(str);

  OS << "\t; SCHEDULED CYCLES: " << MFI->getScheduledCycles(MBB) << "\n";

  OutStreamer.EmitRawText(OS.str());
}

//-----------------------------------------------------------------------------

void TMS320C64XAsmPrinter::EmitFunctionBodyStart() {

  const TMS320C64XMachineFunctionInfo *MFI =
    MF->getInfo<TMS320C64XMachineFunctionInfo>();

  SmallString<128> str;
  raw_svector_ostream OS(str);

  OS << "\t; PRE-PASS CYCLES: " << MFI->getScheduledCyclesPre() << "\n";

  OutStreamer.EmitRawText(OS.str());
}

//-----------------------------------------------------------------------------

extern "C" void LLVMInitializeTMS320C64XAsmPrinter() {
  RegisterAsmPrinter<TMS320C64XAsmPrinter> X(TheTMS320C64XTarget);
}
