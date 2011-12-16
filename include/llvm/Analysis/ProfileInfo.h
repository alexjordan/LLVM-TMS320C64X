//===- llvm/Analysis/ProfileInfo.h - Profile Info Interface -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic ProfileInfo interface, which is used as the
// common interface used by all clients of profiling information, and
// implemented either by making static guestimations, or by actually reading in
// profiling information gathered by running the program.
//
// Note that to be useful, all profile-based optimizations should preserve
// ProfileInfo, which requires that they notify it when changes to the CFG are
// made. (This is not implemented yet.)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PROFILEINFO_H
#define LLVM_ANALYSIS_PROFILEINFO_H

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include <map>
#include <set>

namespace llvm {
  class Pass;
  class raw_ostream;

  class BasicBlock;
  class Function;
  class MachineBasicBlock;
  class MachineFunction;

  // Helper for dumping edges to dbgs().
  raw_ostream& operator<<(raw_ostream &O,
    std::pair<const BasicBlock *, const BasicBlock *> E);

  raw_ostream& operator<<(raw_ostream &O,
    std::pair<const MachineBasicBlock *, const MachineBasicBlock *> E);

  raw_ostream& operator<<(raw_ostream &O, const BasicBlock *BB);
  raw_ostream& operator<<(raw_ostream &O, const MachineBasicBlock *MBB);

  raw_ostream& operator<<(raw_ostream &O, const Function *F);
  raw_ostream& operator<<(raw_ostream &O, const MachineFunction *MF);

//----------------------------------------------------------------------------
/// ProfileInfo Class - This class holds and maintains profiling
/// information for some unit of code.

template<class FType, class BType> class ProfileInfoT {

  public:
    // Types for handling profiling information.
    typedef std::pair<const BType*, const BType*> Edge;
    typedef std::pair<Edge, double> EdgeWeight;
    typedef std::map<Edge, double> EdgeWeights;
    typedef std::map<const BType*, double> BlockCounts;
    typedef std::map<const FType*, double> FuncInfo;
    typedef std::map<const BType*, const BType*> Path;

  protected:
    // EdgeInformation - Count the number of times a transition between two
    // blocks is executed. As a special case, we also hold an edge from the
    // null BasicBlock to the entry block to indicate how many times the
    // function was entered.
    std::map<const FType*, EdgeWeights> EdgeInformation;

    // BlockInformation - Count the number of times a block is executed.
    std::map<const FType*, BlockCounts> BlockInformation;

    // FunctionInformation - Count the number of times a function is executed.
    std::map<const FType*, double> FunctionInformation;

  public:

    static char ID; // Class identification, replacement for typeinfo
    ProfileInfoT();
    ~ProfileInfoT();  // We want to be subclassed

    // MissingValue - The value that is returned for execution counts in case
    // no value is available.
    static const double MissingValue;

    //------------------------------------------------------------------------
    // getFunction:
    // @brief Returns the function for an edge, checking for validity
    // @note This works for both, IR and machine layer analysis types

    static const FType* getFunction(Edge e) {
      if (e.first) {
        return e.first->getParent();
      } else if (e.second) {
        return e.second->getParent();
      }
      assert(0 && "Invalid ProfileInfo::Edge");
      return (const FType*)0;
    }

    //------------------------------------------------------------------------
    // getEdge:
    // @brief Creates an edge from two basic blocks
    // @note This works for both, IR and machine layer analysis types

    static Edge getEdge(const BType *Src, const BType *Dest) {
      return std::make_pair(Src, Dest);
    }

    //===------------------------------------------------------------------===//
    /// Profile Information Queries
    //===------------------------------------------------------------------===//

    double getExecutionCount(const FType *F);
    double getExecutionCount(const BType *BB);

    //------------------------------------------------------------------------
    // setExecutionCount:
    // @brief Set execution count for the given block to the specified value.
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void setExecutionCount(const BType *BB, double w) {
      DEBUG(dbgs() << "Creating Block " << BB->getName()
                   << " (weight: " << format("%.20g",w) << ")\n");
      BlockInformation[BB->getParent()][BB] = w;
    }

    //------------------------------------------------------------------------
    // addExecutionCount:
    // @brief Add an additional count to existing value of specified block
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void addExecutionCount(const BType *BB, double w) {
      double oldw = getExecutionCount(BB);
      assert (oldw != MissingValue
        && "Adding weight to Block with no previous weight");

      DEBUG(dbgs() << "Adding to Block " << BB->getName()
        << " (new weight: " << format("%.20g",oldw + w) << ")\n");
      BlockInformation[BB->getParent()][BB] = oldw + w;
    }

    //------------------------------------------------------------------------
    // getEdgeWeight:
    // @brief Get the execution weight of specified basic-block-edge
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    double getEdgeWeight(Edge e) const {
      typename std::map<const FType*, EdgeWeights>::const_iterator J =
        EdgeInformation.find(getFunction(e));

      if (J == EdgeInformation.end()) return MissingValue;

      typename EdgeWeights::const_iterator I = J->second.find(e);
      if (I == J->second.end()) return MissingValue;

      return I->second;
    }

    //------------------------------------------------------------------------
    // setEdgeWeight:
    // @brief Set the execution weight of specified basic-block-edge
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void setEdgeWeight(Edge e, double w) {
      DEBUG_WITH_TYPE("profile-info", dbgs() << "Creating Edge "
        << e << " (weight: " << format("%.20g",w) << ")\n");
      EdgeInformation[getFunction(e)][e] = w;
    }

    //------------------------------------------------------------------------
    // addEdgeWeight:
    // @brief Increase the execution weight of specified basic-block-edge
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void addEdgeWeight(Edge e, double w) {
      double oldw = getEdgeWeight(e);
      assert (oldw != MissingValue
        && "Adding weight to Edge with no previous weight!");

      DEBUG(dbgs() << "Adding to Edge " << e
        << " (new weight: " << format("%.20g",oldw + w) << ")\n");

      EdgeInformation[getFunction(e)][e] = oldw + w;
    }

    //------------------------------------------------------------------------
    // getEdgeWeights:
    // @brief Return the entire map containing weights of all edges at once
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    EdgeWeights &getEdgeWeights(const FType *F) {
      return EdgeInformation[F];
    }

    //------------------------------------------------------------------------
    // getBlockCounts:
    // @brief Return the entire map containing counts of all blocks at once
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    BlockCounts &getBlockCounts(const FType *F) {
      return BlockInformation[F];
    }

    //------------------------------------------------------------------------
    // getFunctionInfo:
    // @brief Return the entire map containing counts of all functions at once
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    FuncInfo &getFunctionInfo() {
      return FunctionInformation;
    }

    //===------------------------------------------------------------------===//
    /// Analysis Update Methods
    //===------------------------------------------------------------------===//

    //------------------------------------------------------------------------
    // removeBlock:
    // @brief Drop the block from the block-weight map
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void removeBlock(const BType *BB) {
      typename std::map<const Function*, BlockCounts>::iterator J =
        BlockInformation.find(BB->getParent());

      if (J == BlockInformation.end()) return;
      DEBUG(dbgs() << "Deleting " << BB->getName() << "\n");
      J->second.erase(BB);
    }

    //------------------------------------------------------------------------
    // removeEdge:
    // @brief Drop the block-edge from the block-edge-weight map
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void removeEdge(Edge e) {
      typename std::map<const Function*, EdgeWeights>::iterator J =
        EdgeInformation.find(getFunction(e));

      if (J == EdgeInformation.end()) return;
      DEBUG(dbgs() << "Deleting" << e << "\n");
      J->second.erase(e);
    }

    //------------------------------------------------------------------------
    // replaceEdge:
    // @brief Replace the specified edge 'oldedge' by another edge 'newedge'
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here


    void replaceEdge(const Edge &oldedge, const Edge &newedge) {
      double w;

      if ((w = getEdgeWeight(newedge)) == MissingValue) {
        w = getEdgeWeight(oldedge);
        DEBUG(
          dbgs() << "Replacing " << oldedge << " with " << newedge  << "\n");
      }
      else {
        w += getEdgeWeight(oldedge);
        DEBUG(dbgs() << "Adding " << oldedge << " to " << newedge  << "\n");
      }
      setEdgeWeight(newedge,w);
      removeEdge(oldedge);
    }


    enum GetPathMode {
      GetPathToExit = 1,
      GetPathToValue = 2,
      GetPathToDest = 4,
      GetPathWithNewEdges = 8
    };

    //------------------------------------------------------------------------
    // GetPath:
    // @brief Calculate and return a path between specified basic blocks
    // @note Needs to be impelemented properly for both IR and machine layers

    const BType *GetPath(const BType *Src, const BType *Dest,
                              Path &P, unsigned Mode);

    //------------------------------------------------------------------------
    // divertFlow:
    // @brief Divert flow from 'oldedge' to another edge 'newedge'
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void divertFlow(const Edge &oldedge, const Edge &newedge) {
      DEBUG(dbgs() << "Diverting " << oldedge << " via " << newedge );

      // First check if the old edge was taken, if not, just delete it...
      if (getEdgeWeight(oldedge) == 0) {
        removeEdge(oldedge);
        return;
      }

      Path P;
      P[newedge.first] = 0;
      P[newedge.second] = newedge.first;

      const BType *BB = GetPath(newedge.second,
                                oldedge.second,
                                P,
                                GetPathToExit | GetPathToDest);

      double w = getEdgeWeight (oldedge);
      DEBUG(dbgs() << ", Weight: " << format("%.20g",w) << "\n");

      do {
        const BType *Parent = P.find(BB)->second;
        Edge e = getEdge(Parent,BB);

        double oldw = getEdgeWeight(e);
        double oldc = getExecutionCount(e.first);

        setEdgeWeight(e, w+oldw);

        if (Parent != oldedge.first) {
          setExecutionCount(e.first, w+oldc);
        }

        BB = Parent;
      } while (BB != newedge.first);

      removeEdge(oldedge);
    }

    //-------------------------------------------------------------------------
    // splitEdge:
    // @desc Splits an edge in the ProfileInfo and redirects flow over NewBB.
    //   Since its possible that there is more than one edge in the CFG from
    //   FirstBB to SecondBB its necessary to redirect the flow proporionally.

    void splitEdge(const BType *FirstBB, const BType *SecondBB,
                   const BType *NewBB, bool MergeIdenticalEdges = false);

    //-------------------------------------------------------------------------
    // splitBlock:
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void splitBlock(const BType *Old, const BType* New) {
      const FType *F = Old->getParent();
      typename std::map<const Function*, EdgeWeights>::iterator J =
        EdgeInformation.find(F);

      if (J == EdgeInformation.end()) return;

      DEBUG(dbgs() << "Splitting " << Old->getName()
                   << " to " << New->getName() << "\n");

      std::set<Edge> Edges;
      for (typename EdgeWeights::iterator ewi = J->second.begin(),
           ewe = J->second.end(); ewi != ewe; ++ewi)
      {
        Edge old = ewi->first;

        if (old.first == Old) Edges.insert(old);
      }

      for (typename std::set<Edge>::iterator EI = Edges.begin(),
           EE = Edges.end(); EI != EE; ++EI)
      {
        Edge newedge = getEdge(New, EI->second);
        replaceEdge(*EI, newedge);
      }

      double w = getExecutionCount(Old);
      setEdgeWeight(getEdge(Old, New), w);
      setExecutionCount(New, w);
    }

    //-------------------------------------------------------------------------
    // splitBlock:
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void splitBlock(const BType *BB, const BType* NewBB,
                    BType *const *Preds, unsigned NumPreds)
    {
      const MachineFunction *F = BB->getParent();
      typename std::map<const Function*, EdgeWeights>::iterator J =
        EdgeInformation.find(F);

      if (J == EdgeInformation.end()) return;

      DEBUG(dbgs() << "Splitting " << NumPreds << " Edges from "
                   << BB->getName() << " to " << NewBB->getName() << "\n");

      // Collect weight that was redirected over NewBB.
      double newweight = 0;

      std::set<const BType *> ProcessedPreds;

      // For all requestes Predecessors.
      for (unsigned pred = 0; pred < NumPreds; ++pred) {

        const BType * Pred = Preds[pred];
        if (ProcessedPreds.insert(Pred).second) {
          // Create edges and read old weight.
          Edge oldedge = getEdge(Pred, BB);
          Edge newedge = getEdge(Pred, NewBB);

          // Remember how much weight was redirected.
          newweight += getEdgeWeight(oldedge);
          replaceEdge(oldedge,newedge);
        }
      }

      Edge newedge = getEdge(NewBB,BB);
      setEdgeWeight(newedge, newweight);
      setExecutionCount(NewBB, newweight);
    }

    //-------------------------------------------------------------------------
    // transfer:
    // @brief transfer all counts from 'Old' function to 'New' function
    // @note This implementation works for both, IR and machine layer analysis
    // types and has therefore been factored out and moved here

    void transfer(const FType *Old, const FType *New) {
      DEBUG(dbgs() << "Replacing Function " << Old->getName()
                   << " with " << New->getName() << "\n");
      typename std::map<const Function*, EdgeWeights>::iterator J =
        EdgeInformation.find(Old);

      if (J != EdgeInformation.end())
        EdgeInformation[New] = J->second;

      EdgeInformation.erase(Old);
      BlockInformation.erase(Old);
      FunctionInformation.erase(Old);
    }

    // replaceAllUses:
    // @desc Replaces all occurences of RmBB in the ProfilingInfo with DestBB.
    //   This checks all edges of the function the blocks reside in and repla-
    // ces the occurences of RmBB with DestBB.

    void replaceAllUses(const BType *RmBB, const BType *DestBB) {
      DEBUG(dbgs() << "Replacing " << RmBB->getName()
                   << " with " << DestBB->getName() << "\n");

      const FType *F = DestBB->getParent();
      typename std::map<const Function*, EdgeWeights>::iterator J =
        EdgeInformation.find(F);

      if (J == EdgeInformation.end()) return;

      Edge e, newedge;
      bool erasededge = false;

      typename EdgeWeights::iterator I = J->second.begin();
      typename EdgeWeights::iterator E = J->second.end();

      while(I != E) {
        e = (I++)->first;
        bool foundedge = false;
        bool eraseedge = false;

        if (e.first == RmBB) {
          if (e.second == DestBB) {
            eraseedge = true;
          }
          else {
            newedge = getEdge(DestBB, e.second);
            foundedge = true;
          }
        }

        if (e.second == RmBB) {
          if (e.first == DestBB) {
            eraseedge = true;
          }
          else {
            newedge = getEdge(e.first, DestBB);
            foundedge = true;
          }
        }

        if (foundedge) {
          replaceEdge(e, newedge);
        }
        if (eraseedge) {
          if (erasededge) {
            Edge newedge = getEdge(DestBB, DestBB);
            replaceEdge(e, newedge);
          }
          else {
            removeEdge(e);
            erasededge = true;
          }
        }
      }
    }

    void repair(const FType *F);

    void dump(FType *F = 0, bool real = true) {
      dbgs() << "**** This is ProfileInfo " << this << " speaking:\n";
      if (!real) {
        typename std::set<const FType*> Functions;

        dbgs() << "Functions: \n";
        if (F) {
          dbgs() << F << "@" << format("%p", F) << ": " << format("%.20g",getExecutionCount(F)) << "\n";
          Functions.insert(F);
        } else {
          for (typename std::map<const FType*, double>::iterator fi = FunctionInformation.begin(),
               fe = FunctionInformation.end(); fi != fe; ++fi) {
            dbgs() << fi->first << "@" << format("%p",fi->first) << ": " << format("%.20g",fi->second) << "\n";
            Functions.insert(fi->first);
          }
        }

        for (typename std::set<const FType*>::iterator FI = Functions.begin(), FE = Functions.end();
             FI != FE; ++FI) {
          const FType *F = *FI;
          typename std::map<const FType*, BlockCounts>::iterator bwi = BlockInformation.find(F);
          dbgs() << "BasicBlocks for Function " << F << ":\n";
          for (typename BlockCounts::const_iterator bi = bwi->second.begin(), be = bwi->second.end(); bi != be; ++bi) {
            dbgs() << bi->first << "@" << format("%p", bi->first) << ": " << format("%.20g",bi->second) << "\n";
          }
        }

        for (typename std::set<const FType*>::iterator FI = Functions.begin(), FE = Functions.end();
             FI != FE; ++FI) {
          typename std::map<const FType*, EdgeWeights>::iterator ei = EdgeInformation.find(*FI);
          dbgs() << "Edges for Function " << ei->first << ":\n";
          for (typename EdgeWeights::iterator ewi = ei->second.begin(), ewe = ei->second.end(); 
               ewi != ewe; ++ewi) {
            dbgs() << ewi->first << ": " << format("%.20g",ewi->second) << "\n";
          }
        }
      } else {
        assert(F && "No function given, this is not supported!");
        dbgs() << "Functions: \n";
        dbgs() << F << "@" << format("%p", F) << ": " << format("%.20g",getExecutionCount(F)) << "\n";

        dbgs() << "BasicBlocks for Function " << F << ":\n";
        for (typename FType::const_iterator BI = F->begin(), BE = F->end();
             BI != BE; ++BI) {
          const BType *BB = &(*BI);
          dbgs() << BB << "@" << format("%p", BB) << ": " << format("%.20g",getExecutionCount(BB)) << "\n";
        }
      }
      dbgs() << "**** ProfileInfo " << this << ", over and out.\n";
    }

    bool CalculateMissingEdge(const BType *BB, Edge &removed, bool assumeEmptyExit = false);

    bool EstimateMissingEdges(const BType *BB);
/*
    ProfileInfoT<MachineFunction, MachineBasicBlock> *MI() {
      if (MachineProfile == 0)
        MachineProfile = new ProfileInfoT<MachineFunction, MachineBasicBlock>();
      return MachineProfile;
    }

    bool hasMI() const {
      return (MachineProfile != 0);
    }
*/
  };

  typedef ProfileInfoT<Function, BasicBlock> ProfileInfo;
  typedef ProfileInfoT<MachineFunction, MachineBasicBlock> MachineProfileInfo;

  /// createProfileLoaderPass - This function returns a Pass that loads the
  /// profiling information for the module from the specified filename, making
  /// it available to the optimizers.
  Pass *createProfileLoaderPass(const std::string &Filename);
} // End llvm namespace

#endif
