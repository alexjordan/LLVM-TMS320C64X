//===-- llvm/Target/VLIWTargetMachine.h - Target Information ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the VLIWTargetMachine classes.
//
//===----------------------------------------------------------------------===//

#ifndef VLIW_TARGET_TARGETMACHINE_H
#define VLIW_TARGET_TARGETMACHINE_H

#include "llvm/Target/TargetMachine.h"

namespace llvm {

class VLIWTargetMachine : public TargetMachine {
  std::string TargetTriple;
protected: // Can only create subclasses.
  VLIWTargetMachine(const Target &T, const std::string &TargetTriple);
  
private:
  /// addCommonCodeGenPasses - Add standard LLVM codegen passes used for
  /// both emitting to assembly files or machine code output.
  ///
  bool addCommonCodeGenPasses(PassManagerBase &, CodeGenOpt::Level,
                              bool DisableVerify, MCContext *&OutCtx);

  virtual void setCodeModelForJIT();
  virtual void setCodeModelForStatic();
  
public:

  const std::string &getTargetTriple() const { return TargetTriple; }
  
  /// addPassesToEmitFile - Add passes to the specified pass manager to get the
  /// specified file emitted.  Typically this will involve several steps of code
  /// generation.  If OptLevel is None, the code generator should emit code as
  /// fast as possible, though the generated code may be less efficient.
  virtual bool addPassesToEmitFile(PassManagerBase &PM,
                                   formatted_raw_ostream &Out,
                                   CodeGenFileType FileType,
                                   CodeGenOpt::Level,
                                   bool DisableVerify = true);
  
  /// addPassesToEmitMachineCode - Add passes to the specified pass manager to
  /// get machine code emitted.  This uses a JITCodeEmitter object to handle
  /// actually outputting the machine code and resolving things like the address
  /// of functions.  This method returns true if machine code emission is
  /// not supported.
  ///
  virtual bool addPassesToEmitMachineCode(PassManagerBase &PM,
                                          JITCodeEmitter &MCE,
                                          CodeGenOpt::Level,
                                          bool DisableVerify = true);
  
  /// addPassesToEmitMC - Add passes to the specified pass manager to get
  /// machine code emitted with the MCJIT. This method returns true if machine
  /// code is not supported. It fills the MCContext Ctx pointer which can be
  /// used to build custom MCStreamer.
  ///
  virtual bool addPassesToEmitMC(PassManagerBase &PM,
                                 MCContext *&Ctx,
                                 CodeGenOpt::Level OptLevel,
                                 bool DisableVerify = true);

  /// Target-Independent Code Generator Pass Configuration Options.
  
  /// addPreISelPasses - This method should add any "last minute" LLVM->LLVM
  /// passes (which are run just before instruction selector).
  virtual bool addPreISel(PassManagerBase &, CodeGenOpt::Level) {
    return true;
  }

  /// NKim - addPostISel - this hook is provided for passes which need to be
  /// run after the Isel, but before the regalloc or pre-regalloc scheduler
  virtual bool addPostISel(PassManagerBase &, CodeGenOpt::Level) {
    return true;
  }

  /// addInstSelector - This method should install an instruction selector pass,
  /// which converts from LLVM code to machine instructions.
  virtual bool addInstSelector(PassManagerBase &, CodeGenOpt::Level) {
    return true;
  }

  /// VLIW machines require target specific postpass scheduling
  virtual bool addPostRAScheduler(PassManagerBase &, CodeGenOpt::Level) {
    return true;
  }

  /// addPreRegAlloc - This method may be implemented by targets that want to
  /// run passes immediately before register allocation. This should return
  /// true if -print-machineinstrs should print after these passes.
  virtual bool addPreRegAlloc(PassManagerBase &, CodeGenOpt::Level) {
    return false;
  }

  /// addPostRegAlloc - This method may be implemented by targets that want
  /// to run passes after register allocation but before prolog-epilog
  /// insertion.  This should return true if -print-machineinstrs should print
  /// after these passes.
  virtual bool addPostRegAlloc(PassManagerBase &, CodeGenOpt::Level) {
    return false;
  }

  /// addPreSched2 - This method may be implemented by targets that want to
  /// run passes after prolog-epilog insertion and before the second instruction
  /// scheduling pass.  This should return true if -print-machineinstrs should
  /// print after these passes.
  virtual bool addPreSched2(PassManagerBase &, CodeGenOpt::Level) {
    return false;
  }
  
  /// addPreEmitPass - This pass may be implemented by targets that want to run
  /// passes immediately before machine code is emitted.  This should return
  /// true if -print-machineinstrs should print out the code after the passes.
  virtual bool addPreEmitPass(PassManagerBase &, CodeGenOpt::Level) {
    return false;
  }
  
  
  /// addCodeEmitter - This pass should be overridden by the target to add a
  /// code emitter, if supported.  If this is not supported, 'true' should be
  /// returned.
  virtual bool addCodeEmitter(PassManagerBase &, CodeGenOpt::Level,
                              JITCodeEmitter &) {
    return true;
  }

  /// getEnableTailMergeDefault - the default setting for -enable-tail-merge
  /// on this target.  User flag overrides.
  virtual bool getEnableTailMergeDefault() const { return true; }
};

} // End llvm namespace

#endif
