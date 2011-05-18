#ifndef SCALAR_GLOBALIFCONV_H
#define SCALAR_GLOBALIFCONV_H

#include "llvm/Analysis/Interval.h"
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
  GlobalIfConv(llvm::Interval *Int, const IfConv::Oracle &o);
  ~GlobalIfConv();
  void solve(std::set<llvm::BasicBlock*> &result);

};

#endif

