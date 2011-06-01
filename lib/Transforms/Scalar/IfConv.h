#ifndef SCALAR_IFCONV_H
#define SCALAR_IFCONV_H

#include "llvm/BasicBlock.h"
#include "llvm/Analysis/ProfileInfo.h"


namespace IfConv {
  struct BlockInfo {
    bool Convertible;
    unsigned NumInstructions;
    std::set<llvm::Instruction*> SideEffectInsts;
    std::string Name;
    BlockInfo() : Convertible(true), NumInstructions(0) {}
    void dump();
  };

  class Oracle {
  public:
    static const int BRANCH_COST = 3;
    llvm::ProfileInfo *PI;

    Oracle(llvm::ProfileInfo *PI) : PI(PI) {}

    bool shouldConvert(const BlockInfo &host, const BlockInfo &block);
    bool shouldConvert(llvm::BasicBlock *Parent,
        llvm::BasicBlock *If1, llvm::BasicBlock *If2);

    double getEdgeWeight(llvm::BasicBlock *Src, llvm::BasicBlock *Dst);
    double getBBCount(llvm::BasicBlock *BB);
    int getLegCost(llvm::BasicBlock *Parent, llvm::BasicBlock *BB);
    int getConversionCost(llvm::BasicBlock *BB1, llvm::BasicBlock *BB2);

    // global cost model
    int getEdgeCost(llvm::BasicBlock *srcBB, llvm::BasicBlock *dstBB);

    void analyze(llvm::BasicBlock *BB, BlockInfo &info);
  };
}

#endif

