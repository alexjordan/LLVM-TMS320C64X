//===- MachineProfileInfo.cpp - Profile Info Interface --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ProfileInfo interface for machine layer structs
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-profile-info"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#include <queue>
#include <limits>

using namespace llvm;

namespace llvm {

template <>
ProfileInfoT<MachineFunction, MachineBasicBlock>::ProfileInfoT() {}

template <>
ProfileInfoT<MachineFunction, MachineBasicBlock>::~ProfileInfoT() {}

template<>
char ProfileInfoT<MachineFunction, MachineBasicBlock>::ID = 0;

template<> const
double ProfileInfoT<MachineFunction, MachineBasicBlock>::MissingValue = -1;

//-----------------------------------------------------------------------------
// getExecutionCount<MachineBasicBlock>:
// @brief Return execution count for the given MachineBasicBlock

template<> double
MachineProfileInfo::getExecutionCount(const MachineBasicBlock *BB) {

  std::map<const MachineFunction*, BlockCounts>::iterator J =
    BlockInformation.find(BB->getParent());

  // found, count already available
  if (J != BlockInformation.end()) {
    MachineProfileInfo::BlockCounts::iterator I = J->second.find(BB);
    if (I != J->second.end()) return I->second;
  }

  double Count = MissingValue;

  // try predecessors if no count is directly available
  MachineBasicBlock::const_pred_iterator PI = BB->pred_begin();
  MachineBasicBlock::const_pred_iterator PE = BB->pred_end();

  // Are there zero predecessors of this block?
  if (PI == PE) {
    Edge e = getEdge(0, BB);

    Count = getEdgeWeight(e);
  }
  else {
    // Otherwise, if there are predecessors, the execution count of this
    // block is the sum of the edge frequencies from the incoming edges.
    std::set<const MachineBasicBlock*> ProcessedPreds;

    Count = 0;
    for (; PI != PE; ++PI) {
      const MachineBasicBlock *P = *PI;
      if (ProcessedPreds.insert(P).second) {
        double w = getEdgeWeight(getEdge(P, BB));
        if (w == MissingValue) {
          Count = MissingValue;
          break;
        }
        Count += w;
      }
    }
  }

  // If the predecessors did not suffice to get block weight, try successors.
  if (Count == MissingValue) {

    MachineBasicBlock::const_succ_iterator SI = BB->succ_begin();
    MachineBasicBlock::const_succ_iterator SE = BB->succ_end();

    // Are there zero successors of this block?
    if (SI == SE) {
      Edge e = getEdge(BB,0);
      Count = getEdgeWeight(e);
    } else {
      std::set<const MachineBasicBlock*> ProcessedSuccs;
      Count = 0;
      for (; SI != SE; ++SI)
        if (ProcessedSuccs.insert(*SI).second) {
          double w = getEdgeWeight(getEdge(BB, *SI));
          if (w == MissingValue) {
            Count = MissingValue;
            break;
          }
          Count += w;
        }
    }
  }

  if (Count != MissingValue) BlockInformation[BB->getParent()][BB] = Count;
  return Count;
}

//-----------------------------------------------------------------------------
// getExecutionCount<MachineFunction>:
// @brief Return execution count for the given MachineFunction

template<> double
MachineProfileInfo::getExecutionCount(const MachineFunction *MF) {

  std::map<const MachineFunction*, double>::iterator J =
    FunctionInformation.find(MF);

  if (J != FunctionInformation.end())
    return J->second;

  const MachineBasicBlock *entry = &(MF->front());
  assert(!entry->pred_size() && "Invalid entry into a machine function!");

  double Count = getExecutionCount(entry);
  if (Count != MissingValue) FunctionInformation[MF] = Count;
  return Count;
}

//-----------------------------------------------------------------------------
// getPath<MachineFunction, MachineBasicBlock>:
// @brief Calculate and return a path between specified machine basic blocks

template<> const MachineBasicBlock *MachineProfileInfo::GetPath(
                                                 const MachineBasicBlock *Src,
                                                 const MachineBasicBlock *Dest,
                                                 Path &P,
                                                 unsigned Mode)
{
  const MachineBasicBlock *BB = 0;
  bool hasFoundPath = false;

  std::queue<const MachineBasicBlock *> BFS;
  BFS.push(Src);

  while(BFS.size() && !hasFoundPath) {
    BB = BFS.front();
    BFS.pop();

    MachineBasicBlock::const_succ_iterator SI = BB->succ_begin();
    MachineBasicBlock::const_succ_iterator SE = BB->succ_end();

    if (SI == SE) {

      P[0] = BB;
      if (Mode & GetPathToExit) {
        hasFoundPath = true;
        BB = 0;
      }
    }

    for(; SI != SE; ++SI) {

      if (P.find(*SI) != P.end()) continue;

      Edge e = getEdge(BB, *SI);

      if ((Mode & GetPathWithNewEdges)
      && (getEdgeWeight(e) != MissingValue)) continue;

      P[*SI] = BB;
      BFS.push(*SI);

      if ((Mode & GetPathToDest) && *SI == Dest) {
        hasFoundPath = true;
        BB = *SI;
        break;
      }

      if ((Mode & GetPathToValue)
      && (getExecutionCount(*SI) != MissingValue)) {
        hasFoundPath = true;
        BB = *SI;
        break;
      }
    }
  }

  return BB;
}

//-----------------------------------------------------------------------------
// splitEdge<MachineFunction, MachineBasicBlock>:
// @brief Split specified edge between machine basic blocks

template<> void MachineProfileInfo::splitEdge(const MachineBasicBlock *FirstBB,
                                              const MachineBasicBlock *SecondBB,
                                              const MachineBasicBlock *NewBB,
                                              bool MergeIdenticalEdges)
{
  const MachineFunction *F = FirstBB->getParent();
  std::map<const MachineFunction*, EdgeWeights>::iterator J =
    EdgeInformation.find(F);

  if (J == EdgeInformation.end()) return;

  // Generate edges and read current weight.
  Edge e  = getEdge(FirstBB, SecondBB);
  Edge n1 = getEdge(FirstBB, NewBB);
  Edge n2 = getEdge(NewBB, SecondBB);
  EdgeWeights &ECs = J->second;
  double w = ECs[e];

  int succ_count = 0;
  if (!MergeIdenticalEdges) {
    // First count the edges from FristBB to SecondBB, if there is more than
    // one, only slice out a proporional part for NewBB.
    MachineBasicBlock::const_succ_iterator SI = FirstBB->succ_begin();
    MachineBasicBlock::const_succ_iterator SE = FirstBB->succ_end();
    for(; SI != SE; ++SI) if (*SI == SecondBB) succ_count++;

    // When the NewBB is completely new, increment the count by one so that
    // the counts are properly distributed.
    // When the edges are merged anyway, then redirect all flow.
    if (getExecutionCount(NewBB) == ProfileInfo::MissingValue) succ_count++;
  }
  else succ_count = 1;

  // We know now how many edges there are from FirstBB to SecondBB, reroute a
  // proportional part of the edge weight over NewBB.
  double neww = floor(w / succ_count);
  ECs[n1] += neww;
  ECs[n2] += neww;

  BlockInformation[F][NewBB] += neww;

  if (succ_count == 1) ECs.erase(e);
  else ECs[e] -= neww;
}

//-----------------------------------------------------------------------------
// EstimateMissingEdges<MachineFunction, MachineBasicBlock>:
// @brief Estimate missing machine edges
// @note Required in order to be able to repair broken profiles!

template<> bool
MachineProfileInfo::EstimateMissingEdges(const MachineBasicBlock *BB) {
  llvm_unreachable("Estimating missing machine edges not yet implemented!");
}

//-----------------------------------------------------------------------------
// CalculateMissingEdge<MachineFunction, MachineBasicBlock>:
// @brief Calculate a weight for the missing edge
// @note Required in order to be able to repair broken profiles!

template<> bool MachineProfileInfo::CalculateMissingEdge(
                                              const MachineBasicBlock *BB,
                                              Edge &removed,
                                              bool assumeEmptySelf)
{
  llvm_unreachable("Calculating missing machine edges not yet implemented!");
}

//-----------------------------------------------------------------------------
// repair<MachineFunction, MachineBasicBlock>:
// @brief Repair broken profile information for the given machine function

template<> void MachineProfileInfo::repair(const MachineFunction *MF) {
  llvm_unreachable("Repairing PI for machine layer not yet implemented!");
}


} // end of llvm-namespace
