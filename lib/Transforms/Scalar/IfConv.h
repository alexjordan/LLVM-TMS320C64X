#ifndef SCALAR_IFCONV_H
#define SCALAR_IFCONV_H

#include "llvm/BasicBlock.h"

namespace IfConv {
  struct BlockInfo;

  class Oracle {
  public:
    // initialize with block info from the host block
    Oracle() {}
    // ask whether (another) block should be converted into host
    bool shouldConvert(const BlockInfo &host, const BlockInfo &block) const;

    // global cost model
    int getEdgeCost(llvm::BasicBlock *srcBB, llvm::BasicBlock *dstBB) const;

    void analyze(llvm::BasicBlock *BB, BlockInfo &info) const;
  };
}

#endif

