#ifndef SCALAR_GLOBALIFCONV_H
#define SCALAR_GLOBALIFCONV_H

#include "llvm/Analysis/Interval.h"
#include <list>
#include <set>

namespace IfConv {
  class Oracle;
  typedef std::set<llvm::BasicBlock*> CFGPartition_t;
}

namespace CFGPartition {
  class Problem;
}

class GlobalIfConv {
  CFGPartition::Problem *cfgp;
public:
  GlobalIfConv(llvm::Interval *Int, IfConv::Oracle &o);
  ~GlobalIfConv();
  void solve(std::list<IfConv::CFGPartition_t> &result);

};

#endif

