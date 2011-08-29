//===-- VLIWTargetMachine.cpp - Implement the VLIWTargetMachine class -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a target machine for VLIW processors.
//
// Some optimization for compile time were left out and post RA scheduling is
// handled differently.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/VLIWTargetMachine.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetAsmInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/StandardPasses.h"
using namespace llvm;

// subset of options defined by LLVMTargetMachine
extern cl::opt<bool> DisablePostRA;
extern cl::opt<bool> DisableBranchFold;
extern cl::opt<bool> DisableTailDuplicate;
extern cl::opt<bool> DisableEarlyTailDup;
extern cl::opt<bool> DisableCodePlace;
extern cl::opt<bool> DisableSSC;
extern cl::opt<bool> DisableMachineLICM;
extern cl::opt<bool> DisablePostRAMachineLICM;
extern cl::opt<bool> DisableMachineSink;
extern cl::opt<bool> DisableLSR;
extern cl::opt<bool> DisableCGP;
extern cl::opt<bool> PrintLSR;
extern cl::opt<bool> PrintISelInput;
extern cl::opt<bool> PrintGCInfo;
extern cl::opt<bool> ShowMCEncoding;
extern cl::opt<bool> ShowMCInst;
extern cl::opt<bool> EnableMCLogging;
extern cl::opt<bool> VerifyMachineCode;
extern cl::opt<cl::boolOrDefault> AsmVerbose;

static bool getVerboseAsm() {
  switch (AsmVerbose) {
  default:
  case cl::BOU_UNSET: return TargetMachine::getAsmVerbosityDefault();
  case cl::BOU_TRUE:  return true;
  case cl::BOU_FALSE: return false;
  }
}

VLIWTargetMachine::VLIWTargetMachine(const Target &T,
                                     const std::string &Triple)
  : TargetMachine(T), TargetTriple(Triple) {
  AsmInfo = T.createAsmInfo(TargetTriple);
}

// Set the default code model for the JIT for a generic target.
// FIXME: Is small right here? or .is64Bit() ? Large : Small?
void VLIWTargetMachine::setCodeModelForJIT() {
  setCodeModel(CodeModel::Small);
}

// Set the default code model for static compilation for a generic target.
void VLIWTargetMachine::setCodeModelForStatic() {
  setCodeModel(CodeModel::Small);
}

bool VLIWTargetMachine::addPassesToEmitFile(PassManagerBase &PM,
                                            formatted_raw_ostream &Out,
                                            CodeGenFileType FileType,
                                            CodeGenOpt::Level OptLevel,
                                            bool DisableVerify) {
  // Add common CodeGen passes.
  MCContext *Context = 0;
  if (addCommonCodeGenPasses(PM, OptLevel, DisableVerify, Context))
    return true;
  assert(Context != 0 && "Failed to get MCContext");

  const MCAsmInfo &MAI = *getMCAsmInfo();
  OwningPtr<MCStreamer> AsmStreamer;

  switch (FileType) {
  default: return true;
  case CGFT_AssemblyFile: {
    MCInstPrinter *InstPrinter =
      getTarget().createMCInstPrinter(MAI.getAssemblerDialect(), MAI);

    // Create a code emitter if asked to show the encoding.
    MCCodeEmitter *MCE = 0;
    TargetAsmBackend *TAB = 0;
    if (ShowMCEncoding) {
      MCE = getTarget().createCodeEmitter(*this, *Context);
      TAB = getTarget().createAsmBackend(TargetTriple);
    }

    MCStreamer *S = getTarget().createAsmStreamer(*Context, Out,
                                                  getVerboseAsm(),
                                                  hasMCUseLoc(),
                                                  InstPrinter,
                                                  MCE, TAB,
                                                  ShowMCInst);
    AsmStreamer.reset(S);
    break;
  }
  case CGFT_ObjectFile: {
    // Create the code emitter for the target if it exists.  If not, .o file
    // emission fails.
    MCCodeEmitter *MCE = getTarget().createCodeEmitter(*this, *Context);
    TargetAsmBackend *TAB = getTarget().createAsmBackend(TargetTriple);
    if (MCE == 0 || TAB == 0)
      return true;

    AsmStreamer.reset(getTarget().createObjectStreamer(TargetTriple, *Context,
                                                       *TAB, Out, MCE,
                                                       hasMCRelaxAll(),
                                                       hasMCNoExecStack()));
    AsmStreamer.get()->InitSections();
    break;
  }
  case CGFT_Null:
    // The Null output is intended for use for performance analysis and testing,
    // not real users.
    AsmStreamer.reset(createNullStreamer(*Context));
    break;
  }

  if (EnableMCLogging)
    AsmStreamer.reset(createLoggingStreamer(AsmStreamer.take(), errs()));

  // Create the AsmPrinter, which takes ownership of AsmStreamer if successful.
  FunctionPass *Printer = getTarget().createAsmPrinter(*this, *AsmStreamer);
  if (Printer == 0)
    return true;

  // If successful, createAsmPrinter took ownership of AsmStreamer.
  AsmStreamer.take();

  PM.add(Printer);

  // Make sure the code model is set.
  setCodeModelForStatic();
  PM.add(createGCInfoDeleter());
  return false;
}

/// addPassesToEmitMachineCode - Add passes to the specified pass manager to
/// get machine code emitted.  This uses a JITCodeEmitter object to handle
/// actually outputting the machine code and resolving things like the address
/// of functions.  This method should returns true if machine code emission is
/// not supported.
///
bool VLIWTargetMachine::addPassesToEmitMachineCode(PassManagerBase &PM,
                                                   JITCodeEmitter &JCE,
                                                   CodeGenOpt::Level OptLevel,
                                                   bool DisableVerify) {
  // Make sure the code model is set.
  setCodeModelForJIT();

  // Add common CodeGen passes.
  MCContext *Ctx = 0;
  if (addCommonCodeGenPasses(PM, OptLevel, DisableVerify, Ctx))
    return true;

  addCodeEmitter(PM, OptLevel, JCE);
  PM.add(createGCInfoDeleter());

  return false; // success!
}

/// addPassesToEmitMC - Add passes to the specified pass manager to get
/// machine code emitted with the MCJIT. This method returns true if machine
/// code is not supported. It fills the MCContext Ctx pointer which can be
/// used to build custom MCStreamer.
///
bool VLIWTargetMachine::addPassesToEmitMC(PassManagerBase &PM,
                                          MCContext *&Ctx,
                                          CodeGenOpt::Level OptLevel,
                                          bool DisableVerify) {
  // Add common CodeGen passes.
  if (addCommonCodeGenPasses(PM, OptLevel, DisableVerify, Ctx))
    return true;
  // Make sure the code model is set.
  setCodeModelForJIT();

  return false; // success!
}

static void printNoVerify(PassManagerBase &PM, const char *Banner) {
  if (PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(dbgs(), Banner));
}

static void printAndVerify(PassManagerBase &PM,
                           const char *Banner) {
  if (PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(dbgs(), Banner));

  if (VerifyMachineCode)
    PM.add(createMachineVerifierPass(Banner));
}

/// addCommonCodeGenPasses - Add standard LLVM codegen passes used for both
/// emitting to assembly files or machine code output.
///
bool VLIWTargetMachine::addCommonCodeGenPasses(PassManagerBase &PM,
                                               CodeGenOpt::Level OptLevel,
                                               bool DisableVerify,
                                               MCContext *&OutContext) {
  // Standard LLVM-Level Passes.

  // Basic AliasAnalysis support.
  createStandardAliasAnalysisPasses(&PM);

  // Before running any passes, run the verifier to determine if the input
  // coming from the front-end and/or optimizer is valid.
  if (!DisableVerify)
    PM.add(createVerifierPass());

  // Run loop strength reduction before anything else.
  if (OptLevel != CodeGenOpt::None && !DisableLSR) {
    PM.add(createLoopStrengthReducePass(getTargetLowering()));
    if (PrintLSR)
      PM.add(createPrintFunctionPass("\n\n*** Code after LSR ***\n", &dbgs()));
  }

  PM.add(createGCLoweringPass());

  // Make sure that no unreachable blocks are instruction selected.
  PM.add(createUnreachableBlockEliminationPass());

  // Turn exception handling constructs into something the code generators can
  // handle.
  switch (getMCAsmInfo()->getExceptionHandlingType()) {
  case ExceptionHandling::SjLj:
    // SjLj piggy-backs on dwarf for this bit. The cleanups done apply to both
    // Dwarf EH prepare needs to be run after SjLj prepare. Otherwise,
    // catch info can get misplaced when a selector ends up more than one block
    // removed from the parent invoke(s). This could happen when a landing
    // pad is shared by multiple invokes and is also a target of a normal
    // edge from elsewhere.
    PM.add(createSjLjEHPass(getTargetLowering()));
    // FALLTHROUGH
  case ExceptionHandling::DwarfCFI:
  case ExceptionHandling::DwarfTable:
  case ExceptionHandling::ARM:
    PM.add(createDwarfEHPass(this));
    break;
  case ExceptionHandling::None:
    PM.add(createLowerInvokePass(getTargetLowering()));

    // The lower invoke pass may create unreachable code. Remove it.
    PM.add(createUnreachableBlockEliminationPass());
    break;
  }

  if (OptLevel != CodeGenOpt::None && !DisableCGP)
    PM.add(createCodeGenPreparePass(getTargetLowering()));

  PM.add(createStackProtectorPass(getTargetLowering()));

  addPreISel(PM, OptLevel);

  if (PrintISelInput)
    PM.add(createPrintFunctionPass("\n\n"
                                   "*** Final LLVM Code input to ISel ***\n",
                                   &dbgs()));

  // All passes which modify the LLVM IR are now complete; run the verifier
  // to ensure that the IR is valid.
  if (!DisableVerify)
    PM.add(createVerifierPass());

  // Standard Lower-Level Passes.

  // Install a MachineModuleInfo class, which is an immutable pass that holds
  // all the per-module stuff we're generating, including MCContext.
  TargetAsmInfo *TAI = new TargetAsmInfo(*this);
  MachineModuleInfo *MMI = new MachineModuleInfo(*getMCAsmInfo(), TAI);
  PM.add(MMI);
  OutContext = &MMI->getContext(); // Return the MCContext specifically by-ref.

  // Set up a MachineFunction for the rest of CodeGen to work on.
  PM.add(new MachineFunctionAnalysis(*this, OptLevel));

  // Ask the target for an isel.
  if (addInstSelector(PM, OptLevel))
    return true;

  // Print the instruction selected machine code...
  printAndVerify(PM, "After Instruction Selection");

  // Expand pseudo-instructions emitted by ISel.
  PM.add(createExpandISelPseudosPass());

  // Optimize PHIs before DCE: removing dead PHI cycles may make more
  // instructions dead.
  if (OptLevel != CodeGenOpt::None)
    PM.add(createOptimizePHIsPass());

  // If the target requests it, assign local variables to stack slots relative
  // to one another and simplify frame index references where possible.
  PM.add(createLocalStackSlotAllocationPass());

  if (OptLevel != CodeGenOpt::None) {
    // With optimization, dead code should already be eliminated. However
    // there is one known exception: lowered code for arguments that are only
    // used by tail calls, where the tail calls reuse the incoming stack
    // arguments directly (see t11 in test/CodeGen/X86/sibcall.ll).
    PM.add(createDeadMachineInstructionElimPass());
    printAndVerify(PM, "After codegen DCE pass");

    if (!DisableMachineLICM)
      PM.add(createMachineLICMPass());
    PM.add(createMachineCSEPass());
    if (!DisableMachineSink)
      PM.add(createMachineSinkingPass());
    printAndVerify(PM, "After Machine LICM, CSE and Sinking passes");

    PM.add(createPeepholeOptimizerPass());
    printAndVerify(PM, "After codegen peephole optimization pass");
  }

  // Pre-ra tail duplication.
  if (OptLevel != CodeGenOpt::None && !DisableEarlyTailDup) {
    PM.add(createTailDuplicatePass(true));
    printAndVerify(PM, "After Pre-RegAlloc TailDuplicate");
  }

  // Run pre-ra passes.
  if (addPreRegAlloc(PM, OptLevel))
    printAndVerify(PM, "After PreRegAlloc passes");

  // Perform register allocation.
  PM.add(createRegisterAllocator(OptLevel));
  printAndVerify(PM, "After Register Allocation");

  // Perform stack slot coloring and post-ra machine LICM.
  if (OptLevel != CodeGenOpt::None) {
    // FIXME: Re-enable coloring with register when it's capable of adding
    // kill markers.
    if (!DisableSSC)
      PM.add(createStackSlotColoringPass(false));

    // Run post-ra machine LICM to hoist reloads / remats.
    if (!DisablePostRAMachineLICM)
      PM.add(createMachineLICMPass(false));

    printAndVerify(PM, "After StackSlotColoring and postra Machine LICM");
  }

  // Run post-ra passes.
  if (addPostRegAlloc(PM, OptLevel))
    printAndVerify(PM, "After PostRegAlloc passes");

  PM.add(createLowerSubregsPass());
  printAndVerify(PM, "After LowerSubregs");

  // Insert prolog/epilog code.  Eliminate abstract frame index references...
  PM.add(createPrologEpilogCodeInserter());
  printAndVerify(PM, "After PrologEpilogCodeInserter");

  // Run pre-sched2 passes.
  if (addPreSched2(PM, OptLevel))
    printAndVerify(PM, "After PreSched2 passes");

  // Second pass scheduler (required from the target).
  if (addPostRAScheduler(PM, OptLevel))
    return true;
  printAndVerify(PM, "After PostRAScheduler");

  // Branch folding must be run after regalloc and prolog/epilog insertion.
  if (OptLevel != CodeGenOpt::None && !DisableBranchFold) {
    PM.add(createBranchFoldingPass(getEnableTailMergeDefault()));
    printNoVerify(PM, "After BranchFolding");
  }

  // Tail duplication.
  if (OptLevel != CodeGenOpt::None && !DisableTailDuplicate) {
    PM.add(createTailDuplicatePass(false));
    printNoVerify(PM, "After TailDuplicate");
  }

  PM.add(createGCMachineCodeAnalysisPass());

  if (PrintGCInfo)
    PM.add(createGCInfoPrinter(dbgs()));

  if (OptLevel != CodeGenOpt::None && !DisableCodePlace) {
    PM.add(createCodePlacementOptPass());
    printNoVerify(PM, "After CodePlacementOpt");
  }

  if (addPreEmitPass(PM, OptLevel))
    printNoVerify(PM, "After PreEmit passes");

  return false;
}
