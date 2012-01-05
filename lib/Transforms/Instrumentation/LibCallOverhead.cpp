//===-- LibCallOverhead.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Wrap clock counting logic around library calls.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "libcall-overhead"

#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include <set>

using namespace llvm;

namespace {
  class LibCallOverhead : public ModulePass {
    bool runOnModule(Module &M);
  public:
    static char ID;
    GlobalVariable *OverheadCounter;
    LibCallOverhead() : ModulePass(ID) {
      initializeLibCallOverheadPass(*PassRegistry::getPassRegistry());
    }

    virtual const char *getPassName() const {
      return "Libcall overhead instrumentation";
    }

    Value *getCyclesBefore(IRBuilder<> &B);
    Value *getCyclesAfter(IRBuilder<> &B);
    Function *getOrCreateWrapper(CallInst *CI, Function *Callee);

    // XXX subtract this for every call we measure (value as observed in .asm
    // file). we would rather want to place the instrumentation instructions
    // tightly around the call to avoid anomalies, but we need a backend pass
    // for that.
    static const int InstrumentationOverhead = 5;
  };
}

char LibCallOverhead::ID = 0;
INITIALIZE_PASS(LibCallOverhead, "libcall-overhead",
                "Insert instrumentation for libcall overhead counting",
                false, false)

ModulePass *llvm::createLibCallOverheadPass() { return new LibCallOverhead(); }

Value *LibCallOverhead::getCyclesBefore(IRBuilder<> &B) {
  Module *M = B.GetInsertBlock()->getParent()->getParent();
  Value *Clock = M->getOrInsertFunction("llvm.c64x.timestamp.start",
                                        B.getInt32Ty(),
                                        NULL);
  CallInst *CI = B.CreateCall(Clock, "cycles");
  return CI;
}

Value *LibCallOverhead::getCyclesAfter(IRBuilder<> &B) {
  Module *M = B.GetInsertBlock()->getParent()->getParent();
  Value *Clock = M->getOrInsertFunction("llvm.c64x.timestamp.end",
                                        B.getInt32Ty(),
                                        NULL);
  CallInst *CI = B.CreateCall(Clock, "cycles");
  return CI;
}

Function *LibCallOverhead::getOrCreateWrapper(CallInst *CI, Function *Callee) {
  Module *M = Callee->getParent();

  const FunctionType *FT = Callee->getFunctionType();
  int NumParams = FT->getNumParams();
  const Type *RetTy = FT->getReturnType();
  std::vector<const Type*> ArgTys(FT->param_begin(), FT->param_end());

  SmallString<64> typenames;
  if (FT->isVarArg()) {
    assert(Callee->getName().equals("printf"));
    ArgTys.clear();
    for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
      const Type *ty = CI->getArgOperand(i)->getType();
      ArgTys.push_back(ty);
      std::string tyname = ty->getDescription();
      typenames += '_';
      if (ty->isPointerTy()) {
        typenames += "p";
        tyname = tyname.substr(0, tyname.length () - 1);
      }
      typenames += tyname;
    }
    FT = FunctionType::get(RetTy, ArgTys, false);
  }

  SmallString<64> name;
  name += "wrapper_";
  name += Callee->getName();
  name += typenames.c_str();
  name += '_' + utostr(NumParams);

  dbgs() << "new name: " << name << "\n";

  Function *F = Function::Create(FT, Function::ExternalLinkage, name.c_str(), M);
  if (F->getName() != name) {
    F->eraseFromParent();
    return M->getFunction(name);
  }

  SmallVector<Value*, 16> Args;
  // Set names for all arguments.
  unsigned Idx = 0;
  for (Function::arg_iterator AI = F->arg_begin(); Idx != ArgTys.size();
       ++AI, ++Idx) {
    AI->setName("arg" + utostr(Idx));
    dbgs() << "argtype: " << ArgTys[Idx]->getDescription() << "\n";

    // Add arguments to variable symbol table.
    Args.push_back(AI);
  }

  // create a block
  BasicBlock *BB = BasicBlock::Create(M->getContext(), "entry", F);
  IRBuilder<> Builder(F->getContext());
  Builder.SetInsertPoint(BB);

  // get cycles before call
  Value *c1 = getCyclesBefore(Builder);
  // make the call
  Value *RetVal = NULL;
  if (!RetTy->isVoidTy())
    RetVal = Builder.CreateCall(Callee, Args.begin(), Args.end(), "thecall");
  else
    Builder.CreateCall(Callee, Args.begin(), Args.end());
  // calculate and update the cycle overhead
  Value *c2 = getCyclesAfter(Builder);
  Value *difference = Builder.CreateSub(c2, c1, c2->getName());
  Value *duration =
    Builder.CreateSub(difference,
                      ConstantInt::get(Builder.getInt32Ty(),
                                       InstrumentationOverhead), "duration");
  Value *old = Builder.CreateLoad(OverheadCounter, false, "overh");
  Value *updated = Builder.CreateAdd(old, duration, old->getName());
  Builder.CreateStore(updated, OverheadCounter, false);

#if 0
  // for debugging we may print the calculated cycles right away
  std::vector<const Type*> args;
  args.push_back(Builder.getInt8PtrTy());
  FT = FunctionType::get(Builder.getInt32Ty(), args, true);

  Value *printf = M->getOrInsertFunction("printf", FT);
  Value *str = Builder.CreateGlobalString("cycles: %d\n");
  Value *idx[2];
  idx[0] = ConstantInt::get(Builder.getInt32Ty(), 0);
  idx[1] = idx[0];

  Value *gep = Builder.CreateInBoundsGEP(str, idx, idx + 2, "gep");
  Builder.CreateCall2(printf, gep, duration);
#endif

  // return the result of the original call
  if (RetVal)
    Builder.CreateRet(RetVal);
  else
    Builder.CreateRetVoid();
  return F;
}

bool LibCallOverhead::runOnModule(Module &M) {
  const Type *Ty = Type::getInt32Ty(M.getContext());
  OverheadCounter =
    new GlobalVariable(M, Ty, false, GlobalValue::ExternalLinkage,
                       Constant::getNullValue(Ty), "__libcall_overhead");

  std::set<Function *> WrapperFuncs;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (F->isDeclaration()) continue;
    if (WrapperFuncs.count(F)) continue;
    IRBuilder<> Builder(F->getContext());
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ) {
        // Ignore non-calls.
        CallInst *CI = dyn_cast<CallInst>(I++);
        if (!CI) continue;

        // Ignore indirect calls and calls to non-external functions.
        Function *Callee = CI->getCalledFunction();
        if (Callee == 0 || !Callee->isDeclaration() ||
            !(Callee->hasExternalLinkage()))
          continue;

        if (Callee->getName().startswith("llvm.")) {
          dbgs() << "skipping call: " << Callee->getName() << "\n";
          continue;
        }

        dbgs() << "found new call: " << Callee->getName() << "\n";
        Function *Wrapper = getOrCreateWrapper(CI, Callee);
        WrapperFuncs.insert(Wrapper);
        CI->setArgOperand(CI->getNumArgOperands(), Wrapper);
      }
    }
  }
  return true;
}

