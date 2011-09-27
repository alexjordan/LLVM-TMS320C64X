//===- PathProfileInfo.h --------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file outlines the interface used by optimizers to load path profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PATHPROFILEINFO_H
#define LLVM_PATHPROFILEINFO_H

#include "llvm/BasicBlock.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Analysis/PathNumbering.h"
#include "llvm/Analysis/ProfileInfo.h"
#include <stack>
#include <list>

namespace llvm {

//-----------------------------------------------------------------------------

template <class T> class ProfilePathEdgeBase {

  public:
    ProfilePathEdgeBase(T source, T target, unsigned dupNumber)
    : _source(source), _target(target), _duplicateNumber(dupNumber) {}

    inline unsigned getDuplicateNumber() { return _duplicateNumber; }
    inline T getSource() { return _source; }
    inline T getTarget() { return _target; }

  protected:

    T _source;
    T _target;
    unsigned _duplicateNumber;
};

//-----------------------------------------------------------------------------

typedef ProfilePathEdgeBase<BasicBlock *> ProfilePathEdge;
typedef ProfilePathEdgeBase<MachineBasicBlock *> MachineProfilePathEdge;

typedef std::vector<ProfilePathEdge> ProfilePathEdgeVector;
typedef std::vector<ProfilePathEdge>::iterator ProfilePathEdgeIterator;

typedef std::vector<BasicBlock*> ProfilePathBlockVector;
typedef std::vector<BasicBlock*>::iterator ProfilePathBlockIterator;

typedef std::list<MachineBasicBlock*> MachineProfilePathBlockList;

//-----------------------------------------------------------------------------

template <class T> class ProfilePathBase {
protected:
  unsigned int _number;

  ProfilePathBase(unsigned number) : _number(number) {}

public:
  inline unsigned int getNumber() const { return _number; }
  virtual T getFirstBlockInPath() const = 0;
};

//-----------------------------------------------------------------------------

class PathProfileInfo;

class ProfilePath : public ProfilePathBase<BasicBlock *> {
public:
  ProfilePath(unsigned num, unsigned count, double dev, PathProfileInfo *PPI);

  inline unsigned getCount() const { return _count; }
  inline double getCountStdDev() const { return _countStdDev; }
  double getFrequency() const;

  virtual BasicBlock *getFirstBlockInPath() const;

  ProfilePathEdgeVector *getPathEdges() const;
  ProfilePathBlockVector *getPathBlocks() const;

private:
  unsigned _count;
  double _countStdDev;

  // the PPI maintainer
  PathProfileInfo *_ppi;
};

//-----------------------------------------------------------------------------

class MachineProfilePath : public ProfilePathBase<const MachineBasicBlock *> {
public:
  MachineProfilePath(unsigned num, MachineProfilePathBlockList &MBL)
  : ProfilePathBase<const MachineBasicBlock*>(num), BlockList(MBL) {}

  const MachineBasicBlock *getFirstBlockInPath() const {
    return BlockList.size() ? BlockList.front() : 0;
  }

  const MachineProfilePathBlockList &getPathBlocks() const {
    return BlockList;
  }

private:

  // NKim, since we do not provide a Larus/Ball-DAG together with the loader
  // functionality for the machine basic blocks, we maintain a MBB path as a
  // simple (ordered) list for simplicity. There is no direct, convenient and
  // clean way for creating or propagating the profiling information to the
  // machine layer yet. TODO
  MachineProfilePathBlockList BlockList;
};

//-----------------------------------------------------------------------------

typedef std::map<unsigned int,ProfilePath*> ProfilePathMap;
typedef std::map<unsigned int,ProfilePath*>::iterator ProfilePathIterator;

typedef std::map<Function*,unsigned int> FunctionPathCountMap;
typedef std::map<Function*,ProfilePathMap> FunctionPathMap;
typedef std::map<Function*,ProfilePathMap>::iterator FunctionPathIterator;

typedef std::multimap<unsigned, MachineProfilePath> MachineProfilePathMap;

//-----------------------------------------------------------------------------

// TODO: overload [] operator for getting path
// Add: getFunctionCallCount()
class PathProfileInfo {
  public:
  PathProfileInfo();
  ~PathProfileInfo();

  void setCurrentFunction(Function* F);
  Function* getCurrentFunction() const;
  BasicBlock* getCurrentFunctionEntry();

  ProfilePath* getPath(unsigned int number);
  unsigned int getPotentialPathCount();

  ProfilePathIterator pathBegin();
  ProfilePathIterator pathEnd();
  unsigned int pathsRun();

  static char ID; // Pass identification
  std::string argList;

protected:
  FunctionPathMap _functionPaths;
  FunctionPathCountMap _functionPathCounts;

private:
  BallLarusDag* _currentDag;
  Function* _currentFunction;

  friend class ProfilePath;
};

} // end namespace llvm

#endif
