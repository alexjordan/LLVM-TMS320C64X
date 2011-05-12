#ifndef SCALAR_GLOBALIFCONV_H
#define SCALAR_GLOBALIFCONV_H

#include "llvm/Function.h"
#include <set>

namespace IfConv {
  class Oracle;
}

namespace CFGPartition {
  class Problem;
}

class GlobalIfConv {
  CFGPartition::Problem *cfgp;
public:
  GlobalIfConv(llvm::Function &F, const IfConv::Oracle &o);
  ~GlobalIfConv();
  void solve(std::set<llvm::BasicBlock*> &result);

};

#endif

