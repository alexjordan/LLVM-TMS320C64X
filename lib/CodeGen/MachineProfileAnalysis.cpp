//===- MachineProfileInfoLoaderPass.cpp - LLVM Pass to load profile info --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a concrete implementation of profiling information that
// loads the information from a profile dump file.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-profile-analysis"
#include "llvm/Pass.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineProfileAnalysis.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallSet.h"
#include <set>

using namespace llvm;

static cl::opt<double>
EstimatorLoopWeight("machine-profile-estimator-loop-weight", cl::init(100),
  cl::desc("Number of loop executions used for profile-estimator"),
  cl::value_desc("loop-weight")
);

//-----------------------------------------------------------------------------

namespace {

// @class MachineProfileLoader
//
// @desc This is a pass for loading machine profile information. This class
//   inherits and implements the MachineProfileAnalysis interface, performs
//   a loading of edge and path information together and stores the informa-
//   tion appropriately. Since there is no way to profile machine stuff di-
//   rectly, this pass also does some very basic trivial extensions/repairs.

class MachineProfileLoader : public MachineFunctionPass,
                             public MachineProfileAnalysis {

  public:

    static char ID;

    // default object ctor
    MachineProfileLoader()
    : MachineFunctionPass(ID) {
       initializeMachineProfileLoaderPass(*PassRegistry::getPassRegistry());
    }

    // main hook for the "loading" pass. Since we data is actually already
    // loaded (this job is done by corresponding objects within MachinePro-
    // fileAnalysis) we only need to "remap" the IR-structures on machine-
    // basic blocks/functions and paths here
    virtual bool runOnMachineFunction(MachineFunction &MF);

    // return a pointer to the proper object, since we make use of multiple
    // inheritance (MachineFunctionPass and/or MachineProfileAnalysis)
    void * getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &MachineProfileAnalysis::ID)
        return (MachineProfileAnalysis*)this;
      else return this;
    }

    // the pass itself is not destructive, however, since we supply the data
    // by external pointers, we actually also provide a way to clobber any
    // data we want to...
    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    const char *getPassName() const {
      return "Pass for extracting machine profile information";
    }

    // following method is provided for debugging only, it prints the map of
    // all machine basic block paths identified/constructed from the PI
    void emitMachineBlockPaths(MachineFunction &MF) const;

    // following method is provided for debugging only, it prints the content
    // of a basic block path as delivered by the path-profie-info loader pass
    void emitBasicBlockPath(ProfilePath &PP) const;

    // those need to be re-implemented to use static a profile info object
    virtual double getExecutionCount(const MachineBasicBlock *MBB);
    virtual double getExecutionCount(const MachineFunction *MF);
    virtual double getEdgeWeight(MachineProfileInfo::Edge E);
    virtual double getEdgeWeight(const MachineBasicBlock*,
                                 const MachineBasicBlock*);

  private:

    // this stuff is used internally only. These methods ensure the "consis-
    // tency" of the profile-data, resp. applicability of the data onto the
    // current machine cfg. We do not yet patch particular edges and provide
    // only some simple path-reconstruction functionality

    // for cases when a single IR basic block is split into a number of MBBs
    // we can eventually extend the trace by looking for machine blocks which
    // refer to the same IR BB as the tail of the trace. Thats what this func
    // does. It returns a MBB that can be attached to the trace, or 0 if none
    // can be found (which is a regular case for 1:1 relationships)
    MachineBasicBlock *getExtension(MachineBasicBlock &MBB) const;

    // here a couple of checks is performed whether the specified machine bb
    // can be appended/attached to the MBB-path currently being reconstructed
    bool
    isAttachable(MachineProfilePathBlockList&, const MachineBasicBlock&) const;

    // this method inspects the entire function looking for the first machine
    // basic block which corresponds to the specified IR basic block. If it
    // can be found, the MBB is returned, otherwise 0 is returned
    MachineBasicBlock *getMBB(MachineFunction &MF, BasicBlock &BB) const;

    // this method processes one single profile-path of IR basic blocks and
    // tries to extract one (or many) machine basic block paths out of it
    void processTrace(MachineFunction &MF, ProfilePath &PP);

    // for now and only, for any other heavy hacking stuff
    ProfileInfo *getRawProfileInfo() const { return EPI; }
    PathProfileInfo *getRawPathProfileInfo() const { return PPI; }
};

//----------------------------------------------------------------------------

typedef MachineProfileInfo::Edge MachineEdge;

// @class MachineEdgeProfileEstimator
//
// @desc This is a machine-level pendant to the edge-weight estimator for
//   intermediate representation. This actually implements the MachinePro-
//   fileInfo interface, however, due to structural reasons we inherit from
//   the MachineProfileAnalysis for the time being.
//
// @note We do not yet provide an estimation for paths, this however may be
//   included in future

class MachineEdgeProfileEstimator : public MachineFunctionPass,
                                    public MachineProfileAnalysis
{
    double ExecCount;

    MachineLoopInfo *MLI;
    MachineProfileInfo MPI;

    std::set<MachineBasicBlock*> MBBToVisit;
    std::map<MachineLoop*, double> LoopExitWeights;
    std::map<MachineEdge, double>  MinimalWeight;

  public:

    static char ID;

    explicit MachineEdgeProfileEstimator()
    : MachineFunctionPass(ID), ExecCount(EstimatorLoopWeight)
    {
      initializeMachineEdgeProfileEstimatorPass(
        *PassRegistry::getPassRegistry());
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<MachineLoopInfo>();
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    virtual const char *getPassName() const {
      return "Edge profile estimator for machine code";
    }

    /// run - Estimate the profile information from the specified file.
    virtual bool runOnMachineFunction(MachineFunction &F);

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &MachineProfileAnalysis::ID)
        return (MachineProfileAnalysis*)this;
      return this;
    }

    // these are reimplemented to use the machine profile info object
    virtual double getExecutionCount(const MachineBasicBlock *MBB);
    virtual double getExecutionCount(const MachineFunction *MF);
    virtual double getEdgeWeight(MachineProfileInfo::Edge E);
    virtual double getEdgeWeight(const MachineBasicBlock*,
                                 const MachineBasicBlock*);

    void printEdgeError(MachineEdge e, const char *M);
    void printEdgeWeight(MachineEdge e);

  private:

    virtual void recurseMachineBasicBlock(MachineBasicBlock *BB);
};

}  // End of anonymous namespace

// HACK, the loaded intermediate profile data
ProfileInfo *MachineProfileAnalysis::EPI = 0;
PathProfileInfo *MachineProfileAnalysis::PPI = 0;

//----------------------------------------------------------------------------

char MachineProfileAnalysis::ID = 0;
char MachineProfileLoader::ID = 0;
char MachineEdgeProfileEstimator::ID = 0;

// specify default implementation properly if you want to use
// either a loader or an estimator to provide provide information
INITIALIZE_ANALYSIS_GROUP(MachineProfileAnalysis,
  "Machine profile information", MachineProfileLoader)
//  "Machine profile information", MachineEdgeProfileEstimator)

INITIALIZE_AG_PASS(MachineProfileLoader, MachineProfileAnalysis,
  "mach-prof-loader", "Load machine profile information from file", 0, 1, 1)

INITIALIZE_AG_PASS_BEGIN(MachineEdgeProfileEstimator, MachineProfileAnalysis,
  "mach-prof-estimator", "Edge Profile Estimator for Machine code", 1, 1, 1)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_AG_PASS_END(MachineEdgeProfileEstimator, MachineProfileAnalysis,
  "mach-prof-estimator", "Edge Profile Estimator for Machine code", 1, 1, 1)

//----------------------------------------------------------------------------

FunctionPass *llvm::createMachineProfileLoaderPass() {
  return new MachineProfileLoader();
}

FunctionPass *createMachineEdgeProfileEstimatorPass() {
  return new MachineEdgeProfileEstimator();
}

//----------------------------------------------------------------------------
// MachineEdgeProfileEstimator stuff
//----------------------------------------------------------------------------

void MachineEdgeProfileEstimator::printEdgeError(MachineEdge e, const char *M)
{
  DEBUG(dbgs() << "-- Edge " << e << " is not calculated, " << M << "\n");
}

//-----------------------------------------------------------------------------

void MachineEdgeProfileEstimator::printEdgeWeight(MachineEdge E) {
  DEBUG(dbgs() << "-- Weight of Edge " << E << ":"
               << format("%20.20g", getEdgeWeight(E)) << "\n");
}

//----------------------------------------------------------------------------

double
MachineEdgeProfileEstimator::getExecutionCount(const MachineFunction *MF) {
  assert(MF && "Invalid machine function specified for the query!");
  const double count = MPI.getExecutionCount(MF);

  if (count == ProfileInfo::MissingValue)
    DEBUG(dbgs() << "Missing profile estimator count for machine "
      "function '" << MF->getFunction()->getNameStr() << "'!\n");

  return count;
}

//----------------------------------------------------------------------------

double
MachineEdgeProfileEstimator::getExecutionCount(const MachineBasicBlock *MBB) {
  assert(MBB && "Invalid machine basic block specified for the query!");
  const double count = MPI.getExecutionCount(MBB);

  if (count == ProfileInfo::MissingValue)
    DEBUG(dbgs() << "Missing profile estimator count for "
      "machine basic block '" << MBB->getName() << "'!\n");

  return count;
}

//----------------------------------------------------------------------------

double
MachineEdgeProfileEstimator::getEdgeWeight(MachineProfileInfo::Edge E) {
  const double count = MPI.getEdgeWeight(E);

  if (count == ProfileInfo::MissingValue)
    DEBUG(dbgs() << "Missing profile estimator count for machine edge '"
      << E.first->getName() << "', '" << E.second->getName() << "'\n!");

  return count;
}

//----------------------------------------------------------------------------

double
MachineEdgeProfileEstimator::getEdgeWeight(const MachineBasicBlock *MBB1,
                                           const MachineBasicBlock *MBB2)
{
  assert(MBB1 && MBB2 && "Invalid machine basic blocks for the query!");
  return MPI.getEdgeWeight(std::make_pair(MBB1, MBB2));
}

//----------------------------------------------------------------------------

void
MachineEdgeProfileEstimator::recurseMachineBasicBlock(MachineBasicBlock *MBB) {

  // Break the recursion if this BasicBlock was already visited.
  if (MBBToVisit.find(MBB) == MBBToVisit.end()) return;

  MachineProfileInfo::EdgeWeights &edgeWeights =
    MPI.getEdgeWeights(MBB->getParent());

  MachineProfileInfo::BlockCounts &blockCounts =
    MPI.getBlockCounts(MBB->getParent());

  // Read the LoopInfo for this block.
  bool  MBBisHeader = MLI->isLoopHeader(MBB);
  MachineLoop* MBBLoop = MLI->getLoopFor(MBB);

  // To get the block weight, read all incoming edges.
  double MBBWeight = 0;

  std::set<MachineBasicBlock*> ProcessedPreds;

  MachineBasicBlock::pred_iterator PI;
  for (PI = MBB->pred_begin(); PI != MBB->pred_end(); ++PI) {

    // If this block was not considered already, add weight.
    MachineEdge edge = MachineProfileInfo::getEdge(*PI, MBB);

    double w = MPI.getEdgeWeight(edge);

    if (ProcessedPreds.insert(*PI).second) {
      if (w != ProfileInfo::MissingValue) MBBWeight += w;
    }

    // If this block is a loop header and the predecessor is contained in this
    // loop, thus the edge is a backedge, continue and do not check if the
    // value is valid.
    if (MBBisHeader && MBBLoop->contains(*PI)) {
      printEdgeError(edge, "but is backedge, continueing");
      continue;
    }

    // If the edges value is missing (and this is no loop header, and this is
    // no backedge) return, this block is currently non estimatable.
    if (w == ProfileInfo::MissingValue) {
      printEdgeError(edge, "returning");
      return;
    }
  }

  if (MPI.getExecutionCount(MBB) != ProfileInfo::MissingValue) {
    MBBWeight = MPI.getExecutionCount(MBB);
  }

  // Fetch all necessary information for current block.
  SmallVector<MachineEdge, 8> ExitEdges;
  SmallVector<MachineEdge, 8> Edges;

  if (MBBLoop) MBBLoop->getExitEdges(ExitEdges);

  // If this is a loop header, consider the following:
  // Exactly the flow that is entering this block, must exit this block too. So
  // do the following: 
  // *) get all the exit edges, read the flow that is already leaving this
  // loop, remember the edges that do not have any flow on them right now.
  // (The edges that have already flow on them are most likely exiting edges of
  // other loops, do not touch those flows because the previously calculated
  // loopheaders would not be exact anymore.)
  // *) In case there is not a single exiting edge left, create one at the loop
  // latch to prevent the flow from building up in the loop.
  // *) Take the flow that is not leaving the loop already and distribute it on
  // the remaining exiting edges.
  // (This ensures that all flow that enters the loop also leaves it.)
  // *) Increase the flow into the loop by increasing the weight of this block.
  // There is at least one incoming backedge that will bring us this flow later
  // on. (So that the flow condition in this node is valid again.)

  if (MBBisHeader) {

    double incoming = MBBWeight;

    // Subtract the flow leaving the loop.
    std::set<MachineEdge> ProcessedExits;

    SmallVector<MachineEdge, 8>::iterator EI;
    for (EI = ExitEdges.begin(); EI != ExitEdges.end(); ++EI) {

      if (ProcessedExits.insert(*EI).second) {

        double w = MPI.getEdgeWeight(*EI);

        if (w == ProfileInfo::MissingValue) {
          Edges.push_back(*EI);

          // Check if there is a necessary minimal weight, if yes, subtract it 
          // from weight.
          if (MinimalWeight.find(*EI) != MinimalWeight.end()) {
            incoming -= MinimalWeight[*EI];

            DEBUG(dbgs() << "Reserving "
                         << format("%.20g",MinimalWeight[*EI])
                         << " at " << (*EI) << "\n");
          }
        }
        else incoming -= w;
      }
    }
    // If no exit edges, create one:
    if (Edges.size() == 0) {
      MachineBasicBlock *Latch = MBBLoop->getLoopLatch();

      if (Latch) {
        MachineEdge edge = MachineProfileInfo::getEdge(Latch, 0);
        edgeWeights[edge] = MBBWeight;

        printEdgeWeight(edge);
        edge = MachineProfileInfo::getEdge(Latch, MBB);
        edgeWeights[edge] = MBBWeight * ExecCount;
        printEdgeWeight(edge);
      }
    }

    // Distribute remaining weight to the exting edges. To prevent fractions
    // from building up and provoking precision problems the weight which is to
    // be distributed is split and the rounded, the last edge gets a somewhat
    // bigger value, but we are close enough for an estimation.
    double fraction = floor(incoming/Edges.size());

    SmallVector<MachineEdge, 8>::iterator EII;
    for (EII = Edges.begin(); EII != Edges.end(); ++EII) {

      double w = 0;

      if (EII != (Edges.end()-1)) {
        w = fraction;
        incoming -= fraction;
      }
      else w = incoming;

      edgeWeights[*EII] += w;

      // Read necessary minimal weight.
      if (MinimalWeight.find(*EII) != MinimalWeight.end()) {
        edgeWeights[*EII] += MinimalWeight[*EII];

        DEBUG(dbgs() << "Additionally "
                     << format("%.20g",MinimalWeight[*EII])
                     << " at " << (*EII) << "\n");
      }
      printEdgeWeight(*EII);

      // Add minimal weight to paths to all exit edges, this is used to ensure
      // that enough flow is reaching this edges.
      MachineProfileInfo::Path p;

      const MachineBasicBlock *Dest =
        MPI.GetPath(MBB, (*EII).first, p, MachineProfileInfo::GetPathToDest);

      while (Dest != MBB) {

        const MachineBasicBlock *Parent = p.find(Dest)->second;
        MachineEdge e = MachineProfileInfo::getEdge(Parent, Dest);

        if (MinimalWeight.find(e) == MinimalWeight.end()) {
          MinimalWeight[e] = 0;
        }

        MinimalWeight[e] += w;
        DEBUG(dbgs() << "Minimal Weight for " << e << ": "
                     << format("%.20g",MinimalWeight[e]) << "\n");
        Dest = Parent;
      }
    }
    // Increase flow into the loop.
    MBBWeight *= (ExecCount+1);
  }

  blockCounts[MBB] = MBBWeight;

  // Up until now we considered only the loop exiting edges, now we have a
  // definite block weight and must distribute this onto the outgoing edges.
  // Since there may be already flow attached to some of the edges, read this
  // flow first and remember the edges that have still now flow attached.
  Edges.clear();

  std::set<MachineBasicBlock*> ProcessedSuccs;
  MachineBasicBlock::succ_iterator SI = MBB->succ_begin();
  MachineBasicBlock::succ_iterator SE = MBB->succ_end();

  // Also check for (BB,0) edges that may already contain some flow. (But only
  // in case there are no successors.)
  if (SI == SE) {
    MachineEdge edge = MachineProfileInfo::getEdge(MBB,0);
    edgeWeights[edge] = MBBWeight;
    printEdgeWeight(edge);
  }

  for (; SI != SE; ++SI) {

    if (ProcessedSuccs.insert(*SI).second) {

      MachineEdge edge = MachineProfileInfo::getEdge(MBB,*SI);

      double w = MPI.getEdgeWeight(edge);
      if (w != ProfileInfo::MissingValue) {
        MBBWeight -= MPI.getEdgeWeight(edge);
      }
      else {
        Edges.push_back(edge);
        // If minimal weight is necessary, reserve weight by subtracting weight
        // from block weight, this is readded later on.
        if (MinimalWeight.find(edge) != MinimalWeight.end()) {

          MBBWeight -= MinimalWeight[edge];
          DEBUG(dbgs() << "Reserving "
                       << format("%.20g",MinimalWeight[edge])
                       << " at " << edge << "\n");
        }
      }
    }
  }

  double fraction = floor(MBBWeight/Edges.size());

  // Finally we know what flow is still not leaving the block, distribute this
  // flow onto the empty edges.
  SmallVector<MachineEdge, 8>::iterator EI;
  for (EI = Edges.begin(); EI != Edges.end(); ++EI) {

    if (EI != (Edges.end()-1)) {
      edgeWeights[*EI] += fraction;
      MBBWeight -= fraction;
    }
    else edgeWeights[*EI] += MBBWeight;

    // Read minial necessary weight.
    if (MinimalWeight.find(*EI) != MinimalWeight.end()) {
      edgeWeights[*EI] += MinimalWeight[*EI];

      DEBUG(dbgs() << "Additionally "
                   << format("%.20g",MinimalWeight[*EI])
                   << " at " << (*EI) << "\n");
    }
    printEdgeWeight(*EI);
  }

  // This block is visited, mark this before the recursion.
  MBBToVisit.erase(MBB);

  // Recurse into successors.
  MachineBasicBlock::succ_iterator SII;
  for (SII = MBB->succ_begin(); SII != MBB->succ_end(); ++SII)
    recurseMachineBasicBlock(*SI);
}

//----------------------------------------------------------------------------

bool MachineEdgeProfileEstimator::runOnMachineFunction(MachineFunction &MF) {

  // Fetch LoopInfo and clear ProfileInfo for this function.
  MLI = &getAnalysis<MachineLoopInfo>();

  MachineProfileInfo::FuncInfo &funcInfo = MPI.getFunctionInfo();
  MachineProfileInfo::BlockCounts &blockCounts = MPI.getBlockCounts(&MF);
  MachineProfileInfo::EdgeWeights &edgeWeights = MPI.getEdgeWeights(&MF);

  funcInfo.erase(&MF);
  blockCounts.clear();
  edgeWeights.clear();
  MBBToVisit.clear();

  // Mark all blocks as to visit.
  for (MachineFunction::iterator MBB = MF.begin(); MBB != MF.end(); ++MBB)
    MBBToVisit.insert(MBB);

  // Clear Minimal Edges.
  MinimalWeight.clear();

  DEBUG(
    dbgs() << "Estimating function " << MF.getFunction()->getNameStr() << "\n");

  // Since the entry block is the first one and has no predecessors, the edge
  // (0,entry) is inserted with the starting weight of 1. However, for machine
  // functions, there is no dedicated entry block, we assume it to be the first
  // block within the function and check for its predecessors explicitly
  MachineBasicBlock *entry = MF.begin();
  assert(!entry->pred_size() && "Invalid entry into machine function!");

  blockCounts[entry] = pow(2.0, 16.0);
  MachineEdge edge = MachineProfileInfo::getEdge(0, entry);
  edgeWeights[edge] = blockCounts[entry];
  printEdgeWeight(edge);

  // Since recurseBasicBlock() maybe returns with a block which was not fully
  // estimated, use recurseBasicBlock() until everything is calculated.
  bool cleanup = false;
  recurseMachineBasicBlock(entry);

  while (MBBToVisit.size() > 0 && !cleanup) {
    // Remember number of open blocks, this is later used to check if progress
    // was made.
    unsigned size = MBBToVisit.size();

    // Try to calculate all blocks in turn.
    std::set<MachineBasicBlock*>::iterator BI;
    for (BI = MBBToVisit.begin(); BI != MBBToVisit.end(); ++BI) {
      // recurse into block, if at least one block was finished, break
      // because iterator now may become invalid.
      recurseMachineBasicBlock(*BI);
      
      if (MBBToVisit.size() < size) break;
    }

    // If there was not a single block resolved, make some assumptions.
    if (MBBToVisit.size() == size) {

      bool found = false;

      std::set<MachineBasicBlock*>::iterator BBI;
      for (BBI = MBBToVisit.begin(); BBI != MBBToVisit.end() && !found; ++BBI)
      {
        MachineBasicBlock *MBB = *BBI;

        // Try each predecessor if it can be assumed.
        MachineBasicBlock::pred_iterator PI;
        for (PI = MBB->pred_begin(); PI != MBB->pred_end() && !found; ++PI) {
          MachineEdge e = MachineProfileInfo::getEdge(*PI, MBB);

          double w = MPI.getEdgeWeight(e);
          // Check that edge from predecessor is still free.
          if (w == ProfileInfo::MissingValue) {
            // Check if there is a circle from this block to predecessor.
            MachineProfileInfo::Path P;
            const MachineBasicBlock *Dest =
              MPI.GetPath(MBB, *PI, P, MachineProfileInfo::GetPathToDest);

            if (Dest != *PI) {
              // If there is no circle, just set edge weight to 0
              edgeWeights[e] = 0;
              DEBUG(dbgs() << "Assuming edge weight: ");
              printEdgeWeight(e);
              found = true;
            }
          }
        }
      }
      if (!found) {
        cleanup = true;
        DEBUG(dbgs() << "No assumption possible in Function "
                     << MF.getFunction()->getNameStr()
                     << ", setting all to zero\n");
      }
    }
  }
  // In case there was no safe way to assume edges, set as a last measure, 
  // set _everything_ to zero.
  if (cleanup) {

    funcInfo[&MF] = 0;
    blockCounts.clear();
    edgeWeights.clear();

    MachineFunction::const_iterator MFI;
    for (MFI = MF.begin(); MFI != MF.end(); ++MFI) {

      const MachineBasicBlock *MBB = &(*MFI);
      blockCounts[MBB] = 0;

      MachineBasicBlock::const_pred_iterator CPI = MBB->pred_begin();
      MachineBasicBlock::const_pred_iterator CPE = MBB->pred_end();

      if (CPI == CPE) {
        MachineEdge e = MachineProfileInfo::getEdge(0, MBB);
        MPI.setEdgeWeight(e, 0);
      }

      for (; CPI != CPE; ++CPI) {
        MachineEdge e = MachineProfileInfo::getEdge(*CPI, MBB);
        MPI.setEdgeWeight(e, 0);
      }

      MachineBasicBlock::const_succ_iterator SPI = MBB->succ_begin();
      MachineBasicBlock::const_succ_iterator SPE = MBB->succ_end();

      if (SPI == SPE) {
        MachineEdge e = MachineProfileInfo::getEdge(MBB, 0);
        MPI.setEdgeWeight(e, 0);
      }

      for (; SPI != SPE; ++SPI) {
        MachineEdge e = MachineProfileInfo::getEdge(*SPI, MBB);
        MPI.setEdgeWeight(e, 0);
      }
    }
  }

  return false;
}

//----------------------------------------------------------------------------
// MachineProfileLoader stuff
//----------------------------------------------------------------------------

double
MachineProfileLoader::getExecutionCount(const MachineFunction *MF) {
  assert(MF && "Invalid machine function specified for the query!");
  const Function *F = MF->getFunction();
  if (F && EPI) return EPI->getExecutionCount(F);
  else return .0;
}

//----------------------------------------------------------------------------

double
MachineProfileLoader::getExecutionCount(const MachineBasicBlock *MBB) {
  assert(MBB && "Invalid machine block specified for the query!");
  const BasicBlock *BB = MBB->getBasicBlock();
  if (BB && EPI) return EPI->getExecutionCount(BB);
  else return .0;
}

//----------------------------------------------------------------------------

double
MachineProfileLoader::getEdgeWeight(MachineProfileInfo::Edge E) {
  if (!EPI) return .0;

  // check whether we are dealing with a real edge
  const MachineBasicBlock *MBB1 = E.first;
  const MachineBasicBlock *MBB2 = E.second;

  assert(MBB1 && MBB2 && "Invalid edge specified for the query!");
  if (!MBB1->isSuccessor(MBB2)) return .0;
  
  const BasicBlock *BB1 = E.first->getBasicBlock();
  const BasicBlock *BB2 = E.second->getBasicBlock();
  if (!BB1 || !BB2) return .0;

  for (succ_const_iterator I = succ_begin(BB1); I != succ_end(BB1); ++I) {
    if (*I == BB2) return EPI->getEdgeWeight(std::make_pair(BB1, BB2));
  }

  return .0;
}

//----------------------------------------------------------------------------

double MachineProfileLoader::getEdgeWeight(const MachineBasicBlock *MBB1,
                                           const MachineBasicBlock *MBB2)
{
  assert(MBB1 && MBB2 && "Invalid machine basic blocks for the query!");
  return getEdgeWeight(std::make_pair(MBB1, MBB2));
}

//----------------------------------------------------------------------------

void MachineProfileLoader::emitBasicBlockPath(ProfilePath &PP) const {
  ProfilePathBlockVector *blocks = PP.getPathBlocks();
  assert(blocks && "Invalid basic-block-pointer for a profile-trace!");

  if (!blocks->size()) return;

  dbgs() << "basic block path [count " << PP.getCount() << "]:\n";

  ProfilePathBlockIterator BI;
  for (BI = blocks->begin(); BI != blocks->end(); ++BI) {
    if ((*BI)->hasName()) dbgs() << "\t\t" << (*BI)->getName() << '\n';
    else dbgs() << "\t\tunnamed-bb\n";
  }

  dbgs() << '\n';
}

//-----------------------------------------------------------------------------

void MachineProfileLoader::emitMachineBlockPaths(MachineFunction &MF) const {
  for (const_iterator
       I = MachineProfilePaths.begin(); I != MachineProfilePaths.end(); ++I)
  {
    dbgs() << "machine path [count " << I->first << "]:\n";
    const MachineProfilePathBlockList &MBBP = I->second.getPathBlocks();

    MachineProfilePathBlockList::const_iterator J;
    for (J = MBBP.begin(); J != MBBP.end(); ++J)
      dbgs() << "\t\t" << (*J)->getName() << '\n';

    dbgs() << '\n';
  }
}

//-----------------------------------------------------------------------------

MachineBasicBlock*
MachineProfileLoader::getMBB(MachineFunction &MF, BasicBlock &BB) const {

  for (MachineFunction::iterator I = MF.begin(); I != MF.end(); ++I) {

    if (!(I->getBasicBlock())) continue;

    // for the time being consider named blocks only
    if (!(I->getBasicBlock()->hasName())) continue;

    // TODO, to keep things simple, we do not yet consider situations when a
    // basic block is split into multiple machine basic blocks and return the
    // first one encountered in the function-layout. Alternatively we could
    // return a (ordered by block number ?) set of machine basic blocks which
    // correspond to the specified IR block
    if (I->getBasicBlock() == &BB) return &*I;
  }

  return 0;
}

//-----------------------------------------------------------------------------

bool MachineProfileLoader::isAttachable(MachineProfilePathBlockList &MP,
                                        const MachineBasicBlock &MBB) const
{
  // attachable to the empty path
  if (!MP.size()) return true;

  // also attachable to the path, if the specified machine basic block is a
  // successor of the last block stored in the machine path so far, i.e. the
  // trace is not going to be disrupted by insertion of the specified MBB,
  // and also corresponds to the same basic block
  if (MP.back()->isSuccessor(&MBB)) return true;

  // when a basic block just "extends" the tail of the current trace
  if (MP.back()->getBasicBlock() == MBB.getBasicBlock()) return true;

  return false;
}

//-----------------------------------------------------------------------------

MachineBasicBlock *
MachineProfileLoader::getExtension(MachineBasicBlock &MBB) const {

  const BasicBlock *BB = MBB.getBasicBlock();
  assert(BB && "Invalid basic block for MBB!");

  /// now inspect all successors of the given machine basic block and return
  /// a machine basic block which we consider as extension, i.e. a successor
  /// that of the given MBB that refers to the same basic block

  unsigned numExtensions = 0;
  MachineBasicBlock *extensionMBB = 0;

  MachineBasicBlock::succ_iterator SI, SE;
  for (SI = MBB.succ_begin(), SE = MBB.succ_end(); SI != SE; ++SI) {

    const BasicBlock *SBB = (*SI)->getBasicBlock();

    // lost blocks ?
    if (!SBB) continue;

    // avoid trivial cycles, i.e. one block loops. TODO, however, i don't yet
    // check for non-trivial loops, i.e. loops spanning more than 1 machine bb
    // with all of them refering to the same bb...don't know probable this is
    if (*SI == &MBB) continue;

    // now check for any extension-candidates and count them
    if (SBB == BB) { extensionMBB = (*SI); ++numExtensions; }
  }

  // if there are more than 1 successor refering to the same basic block as
  // MBB, we actually can not decide which one to prefer. For simplicity we
  // indicate that there is no extension possible in such cases
  return numExtensions == 1 ? extensionMBB : 0;
}

//-----------------------------------------------------------------------------

void MachineProfileLoader::processTrace(MachineFunction &MF, ProfilePath &PP) {

  ProfilePathBlockVector *blocks = PP.getPathBlocks();
  assert(blocks && "Bad basic blocks for a profile-trace!");

  ProfilePathBlockIterator BI = blocks->begin();

  while (BI != blocks->end()) {

    MachineProfilePathBlockList BlockList;

    // in the most pleasant case there is a true 1:1 relation between a chain
    // of IR basic blocks and the corresponding MBB pendant. However, in most
    // of the cases we expect an IR basic block chain to be split into many
    // MBB-traces

    while (BI != blocks->end()) {

      // first of all inspect the entire function looking for a machine bb,
      // which corresponds to the basic block being currently processed. We
      // do not yet pay attention to cases when multiple machine basic blocks
      // refer to the same bb and simply return the first one which passes a
      // comparison by name
      MachineBasicBlock *MBB = getMBB(MF, **BI);

      // for the time being we require a machine BB to exist
      assert(MBB && "Can't find a MBB for a basic block!");

      if (!isAttachable(BlockList, *MBB)) break;

      // first of all we need to insert the found machine basic block which
      // corresponds to the IR basic block within the current profile path
      BlockList.push_back(MBB);

      std::set<MachineBasicBlock*> processedExtensions;

      // also check, whether we are dealing with a sequence of machine bbs,
      // i.e. a case when one IR basic block is split into multiple machine
      // basic blocks, and extend if possible. We can only extend the trace,
      // if the splitting is sequential and not parallel
      while (1) {
        processedExtensions.insert(MBB);

        // check whether the tail of the trace can be extended, i.e. there is
        // a (unique) MBB-successor that corresponds to the same basic block
        if (MachineBasicBlock *extensionMBB = getExtension(*MBB)) {
          if (!processedExtensions.count(extensionMBB)) {
            processedExtensions.insert(extensionMBB);
            BlockList.push_back(extensionMBB);
            MBB = extensionMBB;
          } else break;
        } else break;
      }

      ++BI;
    }

    if (BlockList.size() > 1) {
      // we don't number paths yet, and also do not make the count to be the
      // the part of the profile-path. Instead, we use the count as a key to
      // the multimap which does a sorting for us
      MachineProfilePath MPP(0, BlockList);
      MachineProfilePaths.insert(std::make_pair(PP.getCount(), MPP));
    }
  }
}

//----------------------------------------------------------------------------

bool MachineProfileLoader::runOnMachineFunction(MachineFunction &MF) {

  // Does not work this way yet, we do not aquire the desired path profile
  // information via getAnalysis but specify the information explicitly
  // during the construction of this pass. This works, but is clearly not
  // very clean, since actually ignoring the pass ordering/dependencies of
  // the LLVM-pass handling framework
  //  PPI = &getAnalysis<PathProfileInfo>();

  DEBUG(dbgs() << "Running MachineProfileLoader on '"
               << MF.getFunction()->getName() << "'\n");

  MachineProfilePaths.clear();

  if (PPI) {
    Function *F = const_cast<Function*>(MF.getFunction());
    assert(F && "Invalid function for the machine code!");

    PPI->setCurrentFunction(F);

    // we can't yet process functions for whose no paths have been recorded,
    // i.e. which have not been executed/profiled, since we do not implement
    // a path calculation itself but rather a reconstruction/verification of
    // an already created/given set of paths
    if (PPI->pathBegin() != PPI->pathEnd()) {
      // now check all profiled paths as delivered by the path-profile-loader
      for (ProfilePathIterator I = PPI->pathBegin(); I != PPI->pathEnd(); ++I)
        processTrace(MF, *(I->second));

      // show reconstructed paths
      DEBUG(emitMachineBlockPaths(MF));
    }
    DEBUG(dbgs() << "Path profile information processed\n");
  }
  else
    // since we are aggreggating edge profile and path profile information
    // in the same loader pass, users may be interested in aquiring edge
    // profiles only, i.e. having the path profile being empty/invalid. We
    // only emit a warning for debug purposes and surpress it by default
    DEBUG(errs() << "Note: no path profile information found/collected!\n");

  return false;
}

//----------------------------------------------------------------------------
// MachineProfileLoader stuff
//----------------------------------------------------------------------------

double
MachineProfileAnalysis::getExecutionCount(const MachineBasicBlock *MBB) {
  llvm_unreachable("No implementation found for looking up block counts!");
}

//----------------------------------------------------------------------------

double MachineProfileAnalysis::getExecutionCount(const MachineFunction *MF) {
  llvm_unreachable("No implementation found for looking up function counts!");
}

//----------------------------------------------------------------------------

double MachineProfileAnalysis::getEdgeWeight(MachineProfileInfo::Edge E) {
  llvm_unreachable("No implementation found for looking up edge counts!");
}

//----------------------------------------------------------------------------

double MachineProfileAnalysis::getEdgeWeight(const MachineBasicBlock *MBB1,
                                             const MachineBasicBlock *MBB2)
{
  llvm_unreachable("No implementation found for looking up edge counts!");
}

