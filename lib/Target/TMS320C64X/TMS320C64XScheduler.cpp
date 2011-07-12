//===-- TMS320C64XScheduler.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Bundling post-register-allocation scheduler for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "post-RA-sched"

#include "TMS320C64X.h"
#include "TMS320C64XMachineFunctionInfo.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/SchedulePostRABase.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLowering.h"

using namespace llvm;

//-----------------------------------------------------------------------------

namespace {

class TMS320C64XScheduler : public MachineFunctionPass {

    TargetMachine &TM;

    bool addTerminatorInstr(MachineBasicBlock *MBB);

  public:

    static char ID;

    TMS320C64XScheduler(TargetMachine &tm)
    : MachineFunctionPass(ID), TM(tm) {}

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineLoopInfo>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    const char *getPassName() const {
      return "C64x+ specific post RA scheduler";
    }

    bool runOnMachineFunction(MachineFunction &Fn);
};

//-----------------------------------------------------------------------------

char TMS320C64XScheduler::ID = 0;

  class CustomListScheduler : public SchedulePostRABase {

    /// HazardRec - The hazard recognizer to use.
    ScheduleHazardRecognizer *HazardRec;

    /// AvailableQueue - The priority queue to use for the available SUnits.
    LatencyPriorityQueue AvailableQueue;

    /// PendingQueue - Instructions which successors have been scheduled, but
    /// are not ready because of their latency.
    std::vector<SUnit*> PendingQueue;

    unsigned NumCycles;

    void ReleasePred(SUnit *SU, const SDep *PredEdge);

    void ScheduleNodeBottomUp(SUnit*, unsigned);

    void ReleasePredecessors(SUnit *SU, unsigned CurCycle);

    bool DelayForLiveRegsBottomUp(SUnit *SU,
                                  SmallVector<unsigned, 4> &LRegs);
    void ListScheduleBottomUp();
  public:

    CustomListScheduler(MachineFunction &MF,
                        const MachineLoopInfo &MLI,
                        const MachineDominatorTree &MDT,
                        ScheduleHazardRecognizer *HR,
                        AliasAnalysis *AA);

    virtual void BuildSchedGraph(AliasAnalysis *AA);
    virtual void Schedule();
    virtual unsigned getCycles() const { return NumCycles; }
    virtual void FixupKills(MachineBasicBlock *MBB) {};
    virtual void Observe(MachineInstr *MI, unsigned Count) {};
    virtual void StartBlock(MachineBasicBlock *BB);
  };
}

//-----------------------------------------------------------------------------

CustomListScheduler::CustomListScheduler(MachineFunction &MF,
                                  const MachineLoopInfo &MLI,
                                  const MachineDominatorTree &MDT,
                                  ScheduleHazardRecognizer *HR,
                                  AliasAnalysis *AA)
  : SchedulePostRABase(MF, MLI, MDT)
  , HazardRec(HR)
  , NumCycles(0)
{}

//-----------------------------------------------------------------------------

/// isSchedulingBoundary - Test if the given instruction should be
/// considered a scheduling boundary. This primarily includes labels
/// and terminators.
///
static bool isSchedulingBoundary(const MachineInstr *MI,
                                 const MachineFunction &MF) {
  // labels can't be scheduled around, terminators get special treatment.
  if (MI->isLabel() || MI->isInlineAsm())
    return true;

  // TMS320C64X specific: usually post-ra scheduling sees stack pointer
  // modifications as a boundary.  we want to weave prolog/epilog into other
  // instructions, so we drop that restriction.
#if 0
  const TargetLowering &TLI = *MF.getTarget().getTargetLowering();
  if (MI->modifiesRegister(TLI.getStackPointerRegisterToSaveRestore()))
    return true;
#endif

  // if these psuedo instructions are used, they are boundaries
  switch (MI->getOpcode()) {
    case TMS320C64X::prolog:
    case TMS320C64X::epilog:
      return true;
  }

  return false;
}

//-----------------------------------------------------------------------------

/// addTerminatorInstr - Add one pseudo-instruction for every branch terminator
/// so scheduling can keep track of the branch delays. The last pseudo
/// instructions marks the actual end of the block schedule (TERM). Returns
/// true if at least one of those instructions was added.

bool TMS320C64XScheduler::addTerminatorInstr(MachineBasicBlock *MBB) {
  MachineBasicBlock::iterator I = MBB->getFirstTerminator();
  if (I == MBB->end())
    return false;

  const TargetInstrInfo *TII = TM.getInstrInfo();

  DebugLoc dl;

  while (I != MBB->end()) {
    BuildMI(*MBB, ++I, dl, TII->get(TMS320C64X::BR_OCCURS));
    // should now point to the next terminator (branch) or end()
    assert(I == MBB->end() ||
           ( I->getOpcode() != TMS320C64X::BR_OCCURS &&
             I->getDesc().isTerminator()));
  }
  // XXX are we supposed to call updateTerminator?
  return true;
}

//-----------------------------------------------------------------------------

bool TMS320C64XScheduler::runOnMachineFunction(MachineFunction &Fn) {
  AliasAnalysis *AA = &getAnalysis<AliasAnalysis>();
  TMS320C64XMachineFunctionInfo *MFI =
    Fn.getInfo<TMS320C64XMachineFunctionInfo>();

  const MachineLoopInfo &MLI = getAnalysis<MachineLoopInfo>();
  const MachineDominatorTree &MDT = getAnalysis<MachineDominatorTree>();
  ScheduleHazardRecognizer *HR =
    TMS320C64XInstrInfo::CreatePostRAHazardRecognizer(&TM);

  // we depend on accurate latencies derived from the itineraries
  assert(TM.getInstrItineraryData() &&
         !TM.getInstrItineraryData()->isEmpty());

  unsigned NumCycles = 0;

  SchedulePostRABase *Scheduler =
    new CustomListScheduler(Fn, MLI, MDT, HR, AA);

  // Loop over all of the basic blocks
  for (MachineFunction::iterator MBB = Fn.begin(), MBBe = Fn.end();
       MBB != MBBe; ++MBB) {
    unsigned BlockCycles = 0;

    // do we add a TERM to the end of the block?
    bool BlockHasTerm = addTerminatorInstr(MBB);

    // Initialize register live-range state for scheduling in this block.
    Scheduler->StartBlock(MBB);

    // Schedule each sequence of instructions not interrupted by a label
    // or anything else that effectively needs to shut down scheduling.
    MachineBasicBlock::iterator Current = MBB->end();
    unsigned Count = MBB->size(), CurrentCount = Count;
    for (MachineBasicBlock::iterator I = Current; I != MBB->begin(); ) {
      MachineInstr *MI = prior(I);
      if (isSchedulingBoundary(MI, Fn)) {
        Scheduler->Run(MBB, I, Current, CurrentCount);
        BlockCycles += Scheduler->getCycles();
        Scheduler->EmitSchedule();
        Current = MI;
        CurrentCount = Count - 1;
        Scheduler->Observe(MI, CurrentCount);
      }
      I = MI;
      --Count;
    }
    assert(Count == 0 && "Instruction count mismatch!");
    assert((MBB->begin() == Current || CurrentCount != 0) &&
           "Instruction count mismatch!");
    Scheduler->Run(MBB, MBB->begin(), Current, CurrentCount);
    BlockCycles += Scheduler->getCycles();
    Scheduler->EmitSchedule();

    if (BlockHasTerm) {
      MachineBasicBlock::iterator LastMI = MBB->end();
      do {
        LastMI = prior(LastMI);
      } while (LastMI->getOpcode() == TMS320C64X::BUNDLE_END);
      assert(LastMI->getOpcode() == TMS320C64X::BR_OCCURS
        && "scheduler moved pseudo-terminator instruction");
    }

    // Clean up register live-range state.
    Scheduler->FinishBlock();

    // Update register kills
    Scheduler->FixupKills(MBB);

    NumCycles += BlockCycles;

    // store cycle count in machine function info
    MFI->setScheduledCycles(MBB, BlockCycles);

  }

  delete Scheduler;
  delete HR;

  return true;
}

/// StartBlock - Initialize register live-range state for scheduling in
/// this block.
///
void CustomListScheduler::StartBlock(MachineBasicBlock *BB) {
  // Call the superclass.
  ScheduleDAGInstrs::StartBlock(BB);

  // Reset the hazard recognizer and anti-dep breaker.
  HazardRec->Reset();
#if BREAK_DEPS
  if (AntiDepBreak != NULL)
    AntiDepBreak->StartBlock(BB);
#endif
}

/// Schedule - Schedule the instruction range using list scheduling.
///
void CustomListScheduler::Schedule() {
  
  // Reset the hazard recognizer (for this sequence).
  HazardRec->Reset();

  // Build the scheduling graph.
#if BREAK_DEPS
  BuildSchedGraph(AA);

  if (AntiDepBreak != NULL) {
    unsigned Broken = 
      AntiDepBreak->BreakAntiDependencies(SUnits, Begin, InsertPos,
                                          InsertPosIndex);
    
    if (Broken != 0) {
      // We made changes. Update the dependency graph.
      // Theoretically we could update the graph in place:
      // When a live range is changed to use a different register, remove
      // the def's anti-dependence *and* output-dependence edges due to
      // that register, and add new anti-dependence and output-dependence
      // edges based on the next live range of the register.
      SUnits.clear();
      Sequence.clear();
      EntrySU = SUnit();
      ExitSU = SUnit();
      BuildSchedGraph(AA);
      
      NumFixedAnti += Broken;
    }
  }
#else
  BuildSchedGraph(NULL);
#endif

  DEBUG(dbgs() << "********** List Scheduling **********\n");
  DEBUG(for (unsigned su = 0, e = SUnits.size(); su != e; ++su)
          SUnits[su].dumpAll(this));

  AvailableQueue.initNodes(SUnits);
  ListScheduleBottomUp();
  AvailableQueue.releaseState();
}

/// ReleasePred - Decrement the NumSuccsLeft count of a predecessor. Add it to
/// the AvailableQueue if the count reaches zero. Also update its cycle bound.
void CustomListScheduler::ReleasePred(SUnit *SU, const SDep *PredEdge) {
  SUnit *PredSU = PredEdge->getSUnit();

#ifndef NDEBUG
  if (PredSU->NumSuccsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    PredSU->dump(this);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(0);
  }
#endif
  --PredSU->NumSuccsLeft;

  // If all the node's successors are scheduled, this node is ready
  // to be scheduled. Ignore the special EntrySU node.
  if (PredSU->NumSuccsLeft == 0 && PredSU != &EntrySU) {
    PendingQueue.push_back(PredSU);
  }
}

void CustomListScheduler::ReleasePredecessors(SUnit *SU, unsigned CurCycle) {
  // Bottom up: release predecessors
  for (SUnit::pred_iterator I = SU->Preds.begin(), E = SU->Preds.end();
       I != E; ++I) {
    ReleasePred(SU, &*I);
#if 0
    if (I->isAssignedRegDep()) {
      // This is a physical register dependency and it's impossible or
      // expensive to copy the register. Make sure nothing that can 
      // clobber the register is scheduled between the predecessor and
      // this node.
      if (!LiveRegDefs[I->getReg()]) {
        ++NumLiveRegs;
        LiveRegDefs[I->getReg()] = I->getSUnit();
        LiveRegCycles[I->getReg()] = CurCycle;
      }
    }
#endif
  }
}

/// ScheduleNodeBottomUp - Add the node to the schedule. Decrement the pending
/// count of its predecessors. If a predecessor pending count is zero, add it to
/// the Available queue.
void CustomListScheduler::ScheduleNodeBottomUp(SUnit *SU, unsigned CurCycle) {
  DEBUG(dbgs() << "*** Scheduling [" << CurCycle << "]: ");
  DEBUG(SU->dump(this));

  assert(CurCycle >= SU->getHeight() && "Node scheduled below its height!");
  SU->setHeightToAtLeast(CurCycle);
  Sequence.push_back(SU);

  ReleasePredecessors(SU, CurCycle);

#if 0
  // Release all the implicit physical register defs that are live.
  for (SUnit::succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    if (I->isAssignedRegDep()) {
      if (LiveRegCycles[I->getReg()] == I->getSUnit()->getHeight()) {
        assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
        assert(LiveRegDefs[I->getReg()] == SU &&
               "Physical register dependency violated?");
        --NumLiveRegs;
        LiveRegDefs[I->getReg()] = NULL;
        LiveRegCycles[I->getReg()] = 0;
      }
    }
  }
#endif

  SU->isScheduled = true;
  AvailableQueue.ScheduledNode(SU);
}

bool CustomListScheduler::DelayForLiveRegsBottomUp(SUnit *SU,
                                             SmallVector<unsigned, 4> &LRegs){
  return false;
}

/// ListScheduleBottomUp - The main loop of list scheduling for bottom-up
/// schedulers.
void CustomListScheduler::ListScheduleBottomUp() {

  unsigned CurCycle = 0;

  HazardRec->Reset();

  // Release any predecessors of the special Exit node.
  ReleasePredecessors(&ExitSU, CurCycle);

  // Add root to Available queue.
  //if (!SUnits.empty()) {
  //  // XXX post-pass hack: rely on the order of the SUnits
  //  SUnit *RootSU = &SUnits[0];
  //  assert(RootSU->Succs.empty() && "Graph root shouldn't have successors!");
  //  RootSU->isAvailable = true;
  //  AvailableQueue.push(RootSU);
  //}

  // In any cycle where we can't schedule any instructions, we must emit a noop.
  bool CycleHasInsts = false;

  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  std::vector<SUnit*> NotReady;
  DenseMap<SUnit*, SmallVector<unsigned, 4> > LRegsMap;
  Sequence.reserve(SUnits.size());

  // We put bundle seperators at the beginnig and end
  Sequence.push_back(getBundleEndSUnit());

  while (!AvailableQueue.empty() || !PendingQueue.empty()) {
    // Check to see if any of the pending instructions are ready to issue.  If
    // so, add them to the available queue.
    for (unsigned i = 0, e = PendingQueue.size(); i != e; ++i) {
      if (PendingQueue[i]->getHeight() <= CurCycle) {
        AvailableQueue.push(PendingQueue[i]);
        PendingQueue[i]->isAvailable = true;
        PendingQueue[i] = PendingQueue.back();
        PendingQueue.pop_back();
        --i; --e;
      }
    }

    DEBUG(dbgs() << "\n*** Examining Available\n";
          LatencyPriorityQueue q = AvailableQueue;
          while (!q.empty()) {
            SUnit *su = q.pop();
            dbgs() << "Height " << su->getHeight() << ": ";
            su->dump(this);
          });

    SUnit *CurSU = AvailableQueue.pop();
    while (CurSU) {
      if (HazardRec->getHazardType(CurSU, 0) ==
          ScheduleHazardRecognizer::NoHazard)
        break;

      CurSU->isPending = true;  // This SU is not in AvailableQueue right now.
      NotReady.push_back(CurSU);
      CurSU = AvailableQueue.pop();
    }

    // Add the nodes that aren't ready back onto the available list.
    if (!NotReady.empty()) {
      AvailableQueue.push_all(NotReady);
      NotReady.clear();
    }

    if (CurSU) {
      // Found instruction that fits current cycle
      ScheduleNodeBottomUp(CurSU, CurCycle);
      HazardRec->EmitInstruction(CurSU);
      CycleHasInsts = true;
    } else if (CycleHasInsts) {
      // No instruction found, but at least one was scheduled in this cycle
      DEBUG(dbgs() << "*** Finished cycle " << CurCycle << '\n');
      Sequence.push_back(getBundleEndSUnit());
      HazardRec->AdvanceCycle();
      ++CurCycle;
      CycleHasInsts = false;
    } else {
      // No instructions in this cycle at all
      DEBUG(dbgs() << "*** Emitting noop in cycle " << CurCycle << '\n');
      Sequence.push_back(0);   // NULL here means noop
      Sequence.push_back(getBundleEndSUnit());
      HazardRec->EmitNoop();
      ++CurCycle;
    }
  } // end while

  // We put bundle seperators at the beginnig and end
  Sequence.push_back(getBundleEndSUnit());

  // Reverse the order if it is bottom up.
  std::reverse(Sequence.begin(), Sequence.end());

  NumCycles = CurCycle;

#ifndef NDEBUG
  VerifySchedule(true);
#endif
}

void CustomListScheduler::BuildSchedGraph(AliasAnalysis *AA) {
 // build the graph as always
  SchedulePostRABase::BuildSchedGraph(AA);

  // Enforce strict order on calls and branches. This also connects our 'branch
  // happens' instruction (TERM) to the actual branch instr.
  SUnit *nextBranch = NULL;
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    // we rely on the reverse ordering of SUnits
    const TargetInstrDesc &tid = SUnits[i].getInstr()->getDesc();
    if (tid.isTerminator() || tid.isCall()) {
      // found something, add order dep
      if (nextBranch)
        nextBranch->addPred(SDep(&SUnits[i], SDep::Order, SUnits[i].Latency));
      nextBranch = &SUnits[i];
    }
    // all branch-related nodes need to be scheduled when becoming available
    if (tid.isTerminator())
      SUnits[i].isScheduleHigh = true;
  }

  bool hasTerm = false;

  // connect last BR_OCCURS to the exit node (0-latency, it is the exit)
  if (SUnits.size() && SUnits[0].getInstr()->getOpcode() == TMS320C64X::BR_OCCURS) {
    // redirect current predecessors
    for (SUnit::pred_iterator I = ExitSU.Preds.begin(), E = ExitSU.Preds.end();
        I != E; ++I)
      SUnits[0].addPred(SDep(I->getSUnit(), SDep::Order, 1));

    ExitSU.addPred(SDep(&SUnits[0], SDep::Order, 0));
    hasTerm = true;
  }

  // attach root nodes to the special exit SU, so scheduling can start off
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    // don't do this twice
    if (SUnits[i].getInstr()->getOpcode() == TMS320C64X::BR_OCCURS)
      continue;

    // It is a root if it has no successors.
    if (SUnits[i].Succs.empty()) {
      // with no pseudo TERM node, the latency to the exit SU can be reduced
      int lat = SUnits[i].Latency;
      if (!hasTerm)
        lat = std::max(0, lat - 1);

      ExitSU.addPred(SDep(&SUnits[i], SDep::Order, lat));
    }
  }
}

FunctionPass *llvm::createTMS320C64XScheduler(TargetMachine &tm) {
  return new TMS320C64XScheduler(tm);
}
