#define DEBUG_TYPE "predication"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Attributes.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "IfConv.h"
#include "GlobalIfConv.h"
#include <list>
using namespace llvm;
using namespace IfConv;

STATISTIC(NumSimpl, "Number of conditions predicated");

static cl::opt<bool>
DumpCFG("pred-dot-cfg", cl::init(false), cl::Hidden,
  cl::desc("Dump annotated graph."));

namespace {
  struct PredicationPass : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    PredicationPass() : FunctionPass(ID) {}

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DominatorTree>();
      AU.addRequiredTransitive<LoopInfo>();
      AU.addPreserved<LoopInfo>();
    }

    struct IfInfo {
      Value *Condition;
      BasicBlock *IfTrue;
      BasicBlock *IfFalse;
    };

    std::set<BasicBlock*> BlocksToPredicate;

    virtual bool runOnFunction(Function &F);
    bool convertPHICycle(BasicBlock *BB, PHINode *PN);
    bool convertPHIs(BasicBlock *BB, PHINode *PN);
    bool predicate(Function &F);
    bool predicateTopDown(Function &F);
    bool simplify(Function &F);
    bool dumpGraph(Function &F);
  };

}

char PredicationPass::ID = 0;
static RegisterPass<PredicationPass> X("predication", "Predicate IFs");

// Public interface to the pass
FunctionPass *llvm::createPredicationPass() {
  return new PredicationPass();
}

/// ChangeToUnreachable - Insert an unreachable instruction before the specified
/// instruction, making it and the rest of the code in the block dead.
#if 0
static void ChangeToUnreachable(Instruction *I) {
  BasicBlock *BB = I->getParent();
  // Loop over all of the successors, removing BB's entry from any PHI
  // nodes.
  for (succ_iterator SI = succ_begin(BB), SE = succ_end(BB); SI != SE; ++SI)
    (*SI)->removePredecessor(BB);
  
  new UnreachableInst(I->getContext(), I);
  
  // All instructions after this are dead.
  BasicBlock::iterator BBI = I, BBE = BB->end();
  while (BBI != BBE) {
    if (!BBI->use_empty())
      BBI->replaceAllUsesWith(UndefValue::get(BBI->getType()));
    BB->getInstList().erase(BBI++);
  }
}
#endif

/// GetIfCondition - Given a basic block (BB) with two predecessors (and
/// presumably PHI nodes in it), check to see if the merge at this block is due
/// to an "if condition".  If so, return the boolean condition that determines
/// which entry into BB will be taken.  Also, return by references the block
/// that will be entered from if the condition is true, and the block that will
/// be entered if the condition is false.
///
///
static Value *GetIfCondition(BasicBlock *BB,
                             BasicBlock *&IfTrue, BasicBlock *&IfFalse) {
  assert(std::distance(pred_begin(BB), pred_end(BB)) == 2 &&
         "Function can only handle blocks with 2 predecessors!");
  BasicBlock *Pred1 = *pred_begin(BB);
  BasicBlock *Pred2 = *++pred_begin(BB);

  // We can only handle branches.  Other control flow will be lowered to
  // branches if possible anyway.
  if (!isa<BranchInst>(Pred1->getTerminator()) ||
      !isa<BranchInst>(Pred2->getTerminator()))
    return 0;
  BranchInst *Pred1Br = cast<BranchInst>(Pred1->getTerminator());
  BranchInst *Pred2Br = cast<BranchInst>(Pred2->getTerminator());

  // Eliminate code duplication by ensuring that Pred1Br is conditional if
  // either are.
  if (Pred2Br->isConditional()) {
    // If both branches are conditional, we don't have an "if statement".  In
    // reality, we could transform this case, but since the condition will be
    // required anyway, we stand no chance of eliminating it, so the xform is
    // probably not profitable.
    if (Pred1Br->isConditional())
      return 0;

    std::swap(Pred1, Pred2);
    std::swap(Pred1Br, Pred2Br);
  }

  if (Pred1Br->isConditional()) {
    // If we found a conditional branch predecessor, make sure that it branches
    // to BB and Pred2Br.  If it doesn't, this isn't an "if statement".
    if (Pred1Br->getSuccessor(0) == BB &&
        Pred1Br->getSuccessor(1) == Pred2) {
      IfTrue = Pred1;
      IfFalse = Pred2;
    } else if (Pred1Br->getSuccessor(0) == Pred2 &&
               Pred1Br->getSuccessor(1) == BB) {
      IfTrue = Pred2;
      IfFalse = Pred1;
    } else {
      // We know that one arm of the conditional goes to BB, so the other must
      // go somewhere unrelated, and this must not be an "if statement".
      return 0;
    }

    // The only thing we have to watch out for here is to make sure that Pred2
    // doesn't have incoming edges from other blocks.  If it does, the condition
    // doesn't dominate BB.
    if (++pred_begin(Pred2) != pred_end(Pred2))
      return 0;

    return Pred1Br->getCondition();
  }

  // Ok, if we got here, both predecessors end with an unconditional branch to
  // BB.  Don't panic!  If both blocks only have a single (identical)
  // predecessor, and THAT is a conditional branch, then we're all ok!
  if (pred_begin(Pred1) == pred_end(Pred1) ||
      ++pred_begin(Pred1) != pred_end(Pred1) ||
      pred_begin(Pred2) == pred_end(Pred2) ||
      ++pred_begin(Pred2) != pred_end(Pred2) ||
      *pred_begin(Pred1) != *pred_begin(Pred2))
    return 0;

  // Otherwise, if this is a conditional branch, then we can use it!
  BasicBlock *CommonPred = *pred_begin(Pred1);
  if (BranchInst *BI = dyn_cast<BranchInst>(CommonPred->getTerminator())) {
    assert(BI->isConditional() && "Two successors but not conditional?");
    if (BI->getSuccessor(0) == Pred1) {
      IfTrue = Pred1;
      IfFalse = Pred2;
    } else {
      IfTrue = Pred2;
      IfFalse = Pred1;
    }
    return BI->getCondition();
  }
  return 0;
}

static void predicateInst(Instruction *I, Value *Cond, bool IsTrue) {
  BasicBlock *BB = I->getParent();
  LLVMContext &ctx = I->getContext();
  Module *M = BB->getParent()->getParent();


  Value *Mem;

  if (StoreInst *St = dyn_cast<StoreInst>(I))
    Mem = St->getPointerOperand();
  else if(LoadInst *Ld = dyn_cast<LoadInst>(I))
    Mem = Ld->getPointerOperand();
  else
    llvm_unreachable("can only predicate load/store");

  const Type *Ty = Mem->getType();
  Function *PredMem =
    Intrinsic::getDeclaration(M, Intrinsic::vliw_predicate_mem, &Ty, 1);
  Value *Ops[] = {
    (IsTrue ? ConstantInt::getTrue(ctx) : ConstantInt::getFalse(ctx)),
    Cond, Mem };

  // insert intrinsic before instruction, then swap places
  Instruction *P = CallInst::Create(PredMem, Ops, Ops + 3, "",  I);
  I->moveBefore(P);
}


bool PredicationPass::convertPHIs(BasicBlock *BB, PHINode *PN) {
  BasicBlock *IfTrue, *IfFalse;
  Function &F = *BB->getParent();

  Value *IfCond = GetIfCondition(BB, IfTrue, IfFalse);
  if (!IfCond)
    return false;

  DEBUG(dbgs() << "FOUND IF:  " << *IfCond << "  T: "
      << IfTrue->getName() << "  F: " << IfFalse->getName() << "\n");

  BasicBlock::iterator AfterPHIIt = BB->begin();
  while (isa<PHINode>(AfterPHIIt))
    AfterPHIIt++;

  BasicBlock *DomBlock = 0, *IfBlock1 = 0, *IfBlock2 = 0;
  BasicBlock *Pred = PN->getIncomingBlock(0);
  if (cast<BranchInst>(Pred->getTerminator())->isUnconditional()) {
    IfBlock1 = Pred;
    DomBlock = *pred_begin(Pred);
  }
  Pred = PN->getIncomingBlock(1);
  if (cast<BranchInst>(Pred->getTerminator())->isUnconditional()) {
    IfBlock2 = Pred;
    DomBlock = *pred_begin(Pred);
  }

  BlockInfo DomBlockInfo, BBInfo[2];
  bool TruePred[2];

  Oracle oracle;
  oracle.analyze(DomBlock, DomBlockInfo);

  if (IfBlock1) {
    oracle.analyze(IfBlock1, BBInfo[0]);
    DEBUG(BBInfo[0].dump());
    if (!oracle.shouldConvert(DomBlockInfo, BBInfo[0]))
      return false;

    DomBlock->getInstList().splice(DomBlock->getTerminator(),
        IfBlock1->getInstList(),
        IfBlock1->begin(),
        IfBlock1->getTerminator());

    TruePred[0] = IfBlock1 == IfTrue;
  }
  if (IfBlock2) {
    oracle.analyze(IfBlock2, BBInfo[1]);
    DEBUG(BBInfo[1].dump());
    if (!oracle.shouldConvert(DomBlockInfo, BBInfo[1]))
      return false;

    DomBlock->getInstList().splice(DomBlock->getTerminator(),
        IfBlock2->getInstList(),
        IfBlock2->begin(),
        IfBlock2->getTerminator());

    TruePred[1] = IfBlock1 == IfTrue;
  }

  if (IfBlock1 && IfBlock2)
    DEBUG(dbgs() << "DIAMOND CONVERSION\n");
  else if (!IfBlock1 && !IfBlock2) {
    DEBUG(dbgs() << "NO BRANCHES, CANCELLING\n");
    return false;
  }
  else
    DEBUG(dbgs() << "LOPSIDED CONVERSION\n");

  for (int i = 0; i < 2; ++i) {
    for (std::set<Instruction*>::iterator SI = BBInfo[i].SideEffectInsts.begin(),
         SE = BBInfo[i].SideEffectInsts.end(); SI != SE; ++SI)
      predicateInst(*SI, IfCond, TruePred[i]);
  }

  while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
    // Change the PHI node into a select instruction.
    Value *TrueVal =
      PN->getIncomingValue(PN->getIncomingBlock(0) == IfFalse);
    Value *FalseVal =
      PN->getIncomingValue(PN->getIncomingBlock(0) == IfTrue);

    //Value *NV = SelectInst::Create(IfCond, TrueVal, FalseVal, "", AfterPHIIt);
    Module *M = F.getParent();
    const Type *Ty = TrueVal->getType();
    Function *IfConvF = Intrinsic::getDeclaration(M, Intrinsic::vliw_ifconv_t, &Ty, 1);
    Value *Ops[] = { IfCond, TrueVal, FalseVal };
    Value *NV = CallInst::Create(IfConvF, Ops, Ops + 3, "psi", AfterPHIIt);

    NV->takeName(PN);
    PN->replaceAllUsesWith(NV);

    BB->getInstList().erase(PN);
  }
  return true;
}

bool PredicationPass::convertPHICycle(BasicBlock *BB, PHINode *PN) {
  BasicBlock *IfTrue, *IfFalse;
  Function &F = *BB->getParent();

  assert(std::distance(pred_begin(BB), pred_end(BB)) == 2 &&
         "Function can only handle blocks with 2 predecessors!");

  DominatorTree &DT = getAnalysis<DominatorTree>();
  LoopInfo &LI = getAnalysis<LoopInfo>();

  pred_iterator PI = pred_begin(BB), E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (BB == *PI)
      return false; // block already branches to itself

    if (!DT.dominates(BB, *PI))
      continue;

    DEBUG(dbgs() << "FOUND PHI CYCLE:\n"
        << BB->getName() << "(d=" << LI.getLoopDepth(BB) << ")" << " --> "
        << Pred->getName() << "(d=" << LI.getLoopDepth(Pred) << ")\n");

    if (Pred->getSinglePredecessor() != BB) {
      DEBUG(dbgs() << "CYCLE TOO LONG\n");
      continue;
    }
    break;
  }

  if (PI == E)
    return false;

  BasicBlock *Pred = *PI;

  BlockInfo HostInfo, PredInfo;
  Oracle oracle;
  oracle.analyze(BB, HostInfo);
  oracle.analyze(Pred, PredInfo);
  if (!oracle.shouldConvert(HostInfo, PredInfo))
    return false;

  BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  assert(BI->isConditional());
  bool TruePred;
  if (BI->getSuccessor(0) == Pred)
    TruePred = true;
  else if (BI->getSuccessor(1) == Pred)
    TruePred = false;
  else
    llvm_unreachable("broken cycle");

  DEBUG(dbgs() << "CONVERT SIMPLE CYCLE\n");
  BB->getInstList().splice(BB->getTerminator(),
      Pred->getInstList(),
      Pred->begin(),
      Pred->getTerminator());

  DEBUG(PredInfo.dump());
  for (std::set<Instruction*>::iterator SI = PredInfo.SideEffectInsts.begin(),
       SE = PredInfo.SideEffectInsts.end(); SI != SE; ++SI)
    predicateInst(*SI, BI->getCondition(), TruePred);

  return true;
}

bool PredicationPass::simplify(Function &F) {
  const TargetData *TD = getAnalysisIfAvailable<TargetData>();

  bool Changed = false;
  bool LocalChange = true;
  while (LocalChange) {
    LocalChange = false;

    for (Function::iterator BBIt = ++F.begin(); BBIt != F.end(); ) {
      if (SimplifyCFG(BBIt++, TD)) {
        LocalChange = true;
        ++NumSimpl;
      }
    }
    Changed |= LocalChange;
  }
  return Changed;
#if 0
  for (Function::iterator BBIt = ++F.begin(); BBIt != F.end(); ++BBIt) {
    BasicBlock *BB = &*BBIt;

    if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      if (BI->isUnconditional()) {
        BasicBlock::iterator BBI = BB->getFirstNonPHI();

        // Ignore dbg intrinsics.
        while (isa<DbgInfoIntrinsic>(BBI))
          ++BBI;
        if (BBI->isTerminator()) // Terminator is the only non-phi instruction!
          if (TryToSimplifyUncondBranchFromEmptyBlock(BB)) {
            return true;
          }
      }
    }
  }
  return false;
#endif
}

bool PredicationPass::predicate(Function &F) {
  bool Changed = false;
  for (Function::iterator BBIt = ++F.begin(); BBIt != F.end(); ++BBIt) {
    BasicBlock *BB = &*BBIt;

    if (PHINode *PN = dyn_cast<PHINode>(BB->begin()))
      if (PN->getNumIncomingValues() == 2) {
        Changed |= convertPHIs(BB, PN);
        Changed |= convertPHICycle(BB, PN);
      }
  }
  return Changed;
}

static void redirectBranch(BasicBlock *BB, BasicBlock *Old, BasicBlock *New) {
  BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  assert(BI);
  for (unsigned i = 0; i < BI->getNumSuccessors(); ++i) {
    if (BI->getSuccessor(i) == Old)
      BI->setSuccessor(i, New);
  }
}

bool PredicationPass::predicateTopDown(Function &F) {
  DominatorTree &DT = getAnalysis<DominatorTree>();

  // BFS
  std::list<BasicBlock*> Worklist;
  std::set<BasicBlock*> Seen;
  std::vector<IfInfo*> IfInfos;
  std::map<BasicBlock*, IfInfo*> Block2If;

  BasicBlock *DomParent = NULL;
  BasicBlock *BB = &*F.begin();
  Worklist.push_back(BB);

  while (Worklist.size()) {
    BB = Worklist.front(); Worklist.pop_front();
    if (Seen.count(BB))
      continue;

    DEBUG(dbgs() << "top-down processing: " << BB->getName() << "\n");
    if (BlocksToPredicate.count(BB)) {
      DomParent = BB;
      break;
    }


    TerminatorInst *TI = BB->getTerminator();
    assert(TI);
    for (unsigned i = 0; i < TI->getNumSuccessors(); ++i)
      Worklist.push_back(TI->getSuccessor(i));
    Seen.insert(BB);
  }

  if (!DomParent)
    return false;

  DEBUG(dbgs() << "DomParent: " << DomParent->getName() << "\n");

  // start again
  Worklist.clear(); Seen.clear();
  Worklist.push_back(DomParent);
  while (Worklist.size()) {
topo_tryagain:
    BB = Worklist.front(); Worklist.pop_front();
    if (Seen.count(BB))
      continue;
    for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
      BasicBlock *PredBB = *PI;
      if (!Seen.count(PredBB)) {
        // unseen predecessor -> not in topological order
        Worklist.push_back(BB);
        goto topo_tryagain;
      }
    }
    Seen.insert(BB);

    // don't touch these blocks other than fixing PHIs
    if (!BlocksToPredicate.count(BB)) {
      // handle join outside the merge region
      typedef std::map<std::set<BasicBlock*>, BasicBlock*> BBMergeMap_t;
      BBMergeMap_t MergeMap;
      BasicBlock::iterator PHIIt = BB->begin();
      while (PHINode *PN = dyn_cast<PHINode>(PHIIt++)) {
        std::set<BasicBlock*> Incoming;
        std::vector<int> ValueNums;
        for (int i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
          BasicBlock *IB = PN->getIncomingBlock(i);
          if (BlocksToPredicate.count(IB)) {
            Incoming.insert(IB);
            ValueNums.push_back(i);
          }
        }
        if (Incoming.size() > 1) {
          // when Incoming.size() == 1 (ie. only one branch was hoisted), we
          // leave the PHI node as it is and let CFG simplification handle it.
          assert(Incoming.size() == 2);
          BasicBlock *MergeBB = NULL;
          BBMergeMap_t::iterator BBMMIt = MergeMap.find(Incoming);
          if (BBMMIt == MergeMap.end()) {
            // insert a merge block
            MergeBB = BasicBlock::Create(F.getContext(), "merge", &F, BB);
            BranchInst::Create(BB, MergeBB);
            DEBUG(dbgs() << "Merge Incoming: ");
            for(std::set<BasicBlock*>::iterator I = Incoming.begin(),
                E = Incoming.end(); I != E; ++I) {
              DEBUG(dbgs() << (*I)->getName() << " ");
              redirectBranch(*I, BB, MergeBB);
            }
            DEBUG(dbgs() << "\n");
            MergeMap.insert(std::make_pair(Incoming, MergeBB));
          } else {
            // there already is a merge block
            MergeBB = BBMMIt->second;
          }
          int i = ValueNums[0]; // assume: i == true-branch
          int j = ValueNums[1]; // j == false-branch

          // find dominating block (XXX recalculate DT??)
          BasicBlock *CommonDom = DT.findNearestCommonDominator(
              PN->getIncomingBlock(i), PN->getIncomingBlock(j));
          assert(CommonDom);
          IfInfo *ii = Block2If[CommonDom];
          if (!DT.dominates(ii->IfTrue, PN->getIncomingBlock(i)))
            std::swap(i, j); // it's the other way around
          assert(DT.dominates(ii->IfTrue, PN->getIncomingBlock(i)));
          assert(DT.dominates(ii->IfFalse, PN->getIncomingBlock(j)));

          Value *TrueVal = PN->getIncomingValue(i);
          Value *FalseVal = PN->getIncomingValue(j);

          Module *M = F.getParent();
          const Type *Ty = TrueVal->getType();
          Function *IfConvF = Intrinsic::getDeclaration(M,
              Intrinsic::vliw_ifconv_t, &Ty, 1);
          Value *Ops[] = { ii->Condition, TrueVal, FalseVal };
          Value *NV = CallInst::Create(IfConvF, Ops, Ops + 3, "psi",
              MergeBB->getTerminator());

          // replace phi operands
          PN->removeIncomingValue(ValueNums[1], false);
          PN->removeIncomingValue(ValueNums[0], false);
          PN->addIncoming(NV, MergeBB);
          //NV->takeName(PN);
        }
      }
      continue;
    }

    // handle join inside the merge region
    while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
      DEBUG(dbgs() << "rewriting phi: " << *PN << "\n");
      std::set<BasicBlock*> Incoming;
      std::vector<int> ValueNums;
      for (int i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
        Incoming.insert(PN->getIncomingBlock(i));
        ValueNums.push_back(i);
      }
      assert(Incoming.size() == 2); // simple if-join

      int i = ValueNums[0]; // assume: i == true-branch
      int j = ValueNums[1]; // j == false-branch

      // find dominating block (XXX recalculate DT??)
      BasicBlock *CommonDom = DT.findNearestCommonDominator(
          PN->getIncomingBlock(i), PN->getIncomingBlock(j));
      assert(CommonDom);
      assert(Block2If.count(CommonDom)); // Ifinfo must exist
      IfInfo *ii = Block2If[CommonDom];
      if (!DT.dominates(ii->IfTrue, PN->getIncomingBlock(i)))
        std::swap(i, j); // it's the other way around
      assert(ii->IfTrue == BB || DT.dominates(ii->IfTrue, PN->getIncomingBlock(i)));
      assert(ii->IfFalse == BB || DT.dominates(ii->IfFalse, PN->getIncomingBlock(j)));

      Value *TrueVal = PN->getIncomingValue(i);
      Value *FalseVal = PN->getIncomingValue(j);

      // fast-forward over PHI nodes
      BasicBlock::iterator AfterPHIIt = BB->begin();
      while (isa<PHINode>(AfterPHIIt))
        AfterPHIIt++;

      Module *M = F.getParent();
      const Type *Ty = TrueVal->getType();
      Function *IfConvF = Intrinsic::getDeclaration(M,
          Intrinsic::vliw_ifconv_t, &Ty, 1);
      if (!ii->Condition->getType()->isIntegerTy(1)) {
        ii->Condition->dump();
        assert(false);
      }
      Value *Ops[] = { ii->Condition, TrueVal, FalseVal };
      Value *NV = CallInst::Create(IfConvF, Ops, Ops + 3, "psi", AfterPHIIt);

      NV->takeName(PN);
      PN->replaceAllUsesWith(NV);

      BB->getInstList().erase(PN);

      DEBUG(dbgs() << "to psi: " << *NV << "\n");
    }

    // stop at a return block
    if (isa<ReturnInst>(BB->getTerminator()))
      continue;

    // hoist code for if-conversion
    if (BB != DomParent) {
      DEBUG(dbgs() << "Hoisting: " << BB->getName() << "\n");
      DomParent->getInstList().splice(DomParent->getTerminator(),
          BB->getInstList(),
          BB->begin(),
          BB->getTerminator());
    }
    BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
    assert(BI);
    if (BI->isConditional()) {
      DEBUG(dbgs() << "Found If: " << *BI << "\n");
      IfInfo *ii = new IfInfo;
      ii->Condition = BI->getCondition();
      ii->IfTrue = BI->getSuccessor(0);
      ii->IfFalse = BI->getSuccessor(1);
      Worklist.push_back(ii->IfTrue);
      Worklist.push_back(ii->IfFalse);
      IfInfos.push_back(ii);
      Block2If.insert(std::make_pair(BB, ii));
    }
    else
      Worklist.push_back(BI->getSuccessor(0));
  }

  // clean up
  for (std::vector<IfInfo*>::iterator I = IfInfos.begin(), E = IfInfos.end();
       I != E; ++ I)
    delete *I;
  return false;
}

bool PredicationPass::runOnFunction(Function &F) {
  if (DumpCFG)
    dumpGraph(F);

  bool Changed;
  do {
    Changed = predicate(F);
    Changed |= simplify(F);
  } while (Changed);

#if 1
  Oracle orcl;
  GlobalIfConv gif(F, orcl);
  gif.solve(BlocksToPredicate);
#else
  // predicate everything
  for (Function::iterator BBIt = F.begin(); BBIt != F.end(); ++BBIt) {
    BasicBlock *BB = &*BBIt;
    BlocksToPredicate.insert(BB);
  }
#endif

  DEBUG(dbgs() << "BLOCKS FOR IF-CONVERSION:\n");
  for (std::set<BasicBlock*>::iterator I = BlocksToPredicate.begin(),
      E = BlocksToPredicate.end(); I != E; ++I)
    DEBUG(dbgs() << (*I)->getNameStr() << "\n");
  DEBUG(dbgs() << "END IF-CONVERSION BLOCKS\n");

  predicateTopDown(F);

  return true;
}

#if 0
  const TargetData *TD = getAnalysisIfAvailable<TargetData>();
  bool EverChanged = RemoveUnreachableBlocksFromFn(F);
  EverChanged |= MergeEmptyReturnBlocks(F);
  EverChanged |= IterativeSimplifyCFG(F, TD);

  // If neither pass changed anything, we're done.
  if (!EverChanged) return false;

  // IterativeSimplifyCFG can (rarely) make some loops dead.  If this happens,
  // RemoveUnreachableBlocksFromFn is needed to nuke them, which means we should
  // iterate between the two optimizations.  We structure the code like this to
  // avoid reruning IterativeSimplifyCFG if the second pass of 
  // RemoveUnreachableBlocksFromFn doesn't do anything.
  if (!RemoveUnreachableBlocksFromFn(F))
    return true;

  do {
    EverChanged = IterativeSimplifyCFG(F, TD);
    EverChanged |= RemoveUnreachableBlocksFromFn(F);
  } while (EverChanged);
#endif

bool
IfConv::Oracle::shouldConvert(const BlockInfo &host, const BlockInfo &block) const {
  // simple and state-less: do not convert any blocks larger than 10 insts.
  if (!block.Convertible)
    return false;

  if (block.NumInstructions > 10) {
    DEBUG(dbgs() << block.Name <<  "too big (" << block.NumInstructions << ")\n");
    return false;
  }
  return true;
}

int
IfConv::Oracle::getEdgeCost(llvm::BasicBlock *srcBB, llvm::BasicBlock *dstBB) const {
  BlockInfo srcInfo, dstInfo;
  analyze(srcBB, srcInfo);
  analyze(dstBB, dstInfo);

  return 10;
}

void IfConv::Oracle::analyze(BasicBlock *BB, BlockInfo &info) const {
  info.Name = BB->getNameStr();
  info.Convertible = true;
  for (BasicBlock::iterator BBI = BB->begin(), BBE = BB->end(); BBI != BBE; ++BBI) {
    Instruction *I = BBI;

    if (I->isTerminator() || I->getOpcode() == Instruction::PHI)
      break; // ok for us, branch semantics checked elsewhere

    // count as instruction
    ++info.NumInstructions;

    if (I->isSafeToSpeculativelyExecute())
      continue;
    switch (I->getOpcode()) {
    default:
      DEBUG(dbgs() << "cannot predicate: " << *I << "\n");
      info.Convertible = false;
      break;
    case Instruction::Call: {
      CallInst *call = dyn_cast<CallInst>(I);
      Function *callee = call->getCalledFunction();
      if (!callee || !callee->isIntrinsic() ||
          callee->getIntrinsicID() != Intrinsic::vliw_ifconv_t)
        info.Convertible = false;
      break;
    }
    case Instruction::Store:
    case Instruction::Load:
      info.SideEffectInsts.insert(I);
      break;
    }
  }
}

void IfConv::BlockInfo::dump() {
      dbgs() << "--- Block: " << Name << " -------\n";
      dbgs() << "Convertible: " << (Convertible ? "true" : "false") << "\n";
      dbgs() << "Instructions: " << NumInstructions << "\n";
      dbgs() << "Instructions w/ side-effects: "
        << SideEffectInsts.size() << "\n";
      dbgs() << "---------------------\n";
}

//
// Graph Output
//

#include "llvm/Support/GraphWriter.h"

namespace {
  static LoopInfo *WriterLI;
}

namespace llvm {
template<>
struct DOTGraphTraits<const Function*> : public DefaultDOTGraphTraits {

  DOTGraphTraits() : DefaultDOTGraphTraits() {}
  DOTGraphTraits(bool foo) : DefaultDOTGraphTraits() {}

  static std::string getGraphName(const Function *F) {
    return "CFG for '" + F->getNameStr() + "' function";
  }

  std::string getNodeLabel(const BasicBlock *Node,
                           const Function *Graph) {
    std::string Str;
    raw_string_ostream OS(Str);

    WriteAsOperand(OS, Node, false);
    OS << " (" << WriterLI->getLoopDepth(Node) << ")";
    return OS.str();
  }
};
}

bool PredicationPass::dumpGraph(Function &F) {
  std::string Filename = "cfg.pred." + F.getNameStr() + ".dot";
  errs() << "Writing '" << Filename << "'...";

  std::string ErrorInfo;
  raw_fd_ostream File(Filename.c_str(), ErrorInfo);

  WriterLI = &getAnalysis<LoopInfo>();

  if (ErrorInfo.empty())
    WriteGraph(File, (const Function*)&F);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
  return false;
}
