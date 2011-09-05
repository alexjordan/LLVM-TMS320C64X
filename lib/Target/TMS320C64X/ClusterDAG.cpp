//===-- TMS320C64X/ClusterDAG.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines cluster assignment helper for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cluster-assignment"
#include "ClusterDAG.h"
#include "TMS320C64XInstrInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetSubtarget.h"
#include "llvm/ADT/Statistic.h"

#include "llvm/Support/Debug.h"
#undef DEBUG
#define DEBUG(x) x

using namespace llvm;
using namespace llvm::TMS320C64X;

STATISTIC(XccStalls, "Number of XPATH stalls avoided");
STATISTIC(XccInserted, "Number of cross-cluster copies inserted");

ClusterDAG::~ClusterDAG() {
  for (std::vector<SUnit*>::iterator I = CopySUs.begin(), E = CopySUs.end();
       I != E; ++I)
    delete *I;
}

void ClusterDAG::BuildSchedGraph(AliasAnalysis *AA) {
  // Most of this code is copied from ScheduleDAGInstrs::BuildSchedGraph. We
  // needed to make small changes; this version is supposed to build a
  // single graph for the whole block under consideration. We went for a
  // modified copy of that code since modifying it directly would break the
  // postpass scheduler.

  // We'll be allocating one SUnit for each instruction, plus one for
  // the region exit node.
  SUnits.reserve(BB->size());

  // We build scheduling units by walking a block's instruction list from bottom
  // to top.

  // Remember where a generic side-effecting instruction is as we procede.
  SUnit *BarrierChain = 0, *AliasChain = 0;

  // Memory references to specific known memory locations are tracked
  // so that they can be given more precise dependencies. We track
  // separately the known memory locations that may alias and those
  // that are known not to alias
  std::map<const Value *, SUnit *> AliasMemDefs, NonAliasMemDefs;
  std::map<const Value *, std::vector<SUnit *> > AliasMemUses, NonAliasMemUses;

  // Keep track of dangling debug references to registers.
  // GB: changed 3 lines
  // std::map<unsigned, std::pair<MachineInstr*, unsigned> >
  //   DanglingDebugValue(TRI->getNumRegs(),
  //   std::make_pair(static_cast<MachineInstr*>(0), 0));
  // GB: added 3 lines
  std::map<unsigned, std::pair<MachineInstr*, unsigned> > DanglingDebugValue;
  std::map<unsigned, std::vector<SUnit *> > Defs;
  std::map<unsigned, std::vector<SUnit *> > Uses;

  // Check to see if the scheduler cares about latencies.
  bool UnitLatencies = ForceUnitLatencies();

  // Ask the target if address-backscheduling is desirable, and if so how much.
  const TargetSubtarget &ST = TM.getSubtarget<TargetSubtarget>();
  unsigned SpecialAddressLatency = ST.getSpecialAddressLatency();

  // Remove any stale debug info; sometimes BuildSchedGraph is called again
  // without emitting the info from the previous call.
  DbgValueVec.clear();

  // Model data dependencies between instructions being scheduled and the
  // ExitSU.
  // AJO: don't bother
  //AddSchedBarrierDeps();

  // Walk the list of instructions, from bottom moving up.
  // GB: added 1 line
  SUnit *PreviousBarrier = NULL;
  for (MachineBasicBlock::iterator MII = InsertPos, MIE = Begin;
       MII != MIE; --MII) {
    MachineInstr *MI = prior(MII);
    // DBG_VALUE does not have SUnit's built, so just remember these for later
    // reinsertion.
    if (MI->isDebugValue()) {
      if (MI->getNumOperands()==3 && MI->getOperand(0).isReg() &&
          MI->getOperand(0).getReg())
        DanglingDebugValue[MI->getOperand(0).getReg()] =
             std::make_pair(MI, DbgValueVec.size());
      DbgValueVec.push_back(MI);
      continue;
    }
    const TargetInstrDesc &TID = MI->getDesc();
    // GB: changed 2 lines
    // assert(!TID.isTerminator() && !MI->isLabel() &&
    //        "Cannot schedule terminators or labels!");
    // GB: added 17 lines
    // For barrier instructions, build a barrier SUnit. That's a normal
    // SUnit, except that we insert extra dependences to make sure that
    // previously created SUnits will not be scheduled past this barrier.
    if (TID.isTerminator() || MI->isLabel() ||
        TII->isSchedulingBoundary(MI, BB, MF)) {
      SUnit *BarrierSU = NewSUnit(MI);
      BarrierSU->Latency = (UnitLatencies ? 1 : 0);
      SUnit *I = (PreviousBarrier ? PreviousBarrier : &SUnits.front());
      while (I != BarrierSU) {
        if (I->Preds.empty()) {
          I->addPred(SDep(BarrierSU, SDep::Order, /* latency = */ 0));
        }
        I++;
      }
      PreviousBarrier = BarrierSU;
      continue;
    }
    // Create the SUnit for this MI.
    SUnit *SU = NewSUnit(MI);
    SU->isCall = TID.isCall();
    SU->isCommutable = TID.isCommutable();

    // Assign the Latency field of SU using target-provided information.
    if (UnitLatencies)
      SU->Latency = 1;
    else
      ComputeLatency(SU);

    // Add register-based dependencies (data, anti, and output).
    for (unsigned j = 0, n = MI->getNumOperands(); j != n; ++j) {
      const MachineOperand &MO = MI->getOperand(j);
      if (!MO.isReg()) continue;
      unsigned Reg = MO.getReg();
      if (Reg == 0) continue;

      // GB: changed 1 lines
      // assert(TRI->isPhysicalRegister(Reg)&&"Virtual register encountered!");

      if (MO.isDef() && DanglingDebugValue[Reg].first!=0) {
        SU->DbgInstrList.push_back(DanglingDebugValue[Reg].first);
        DbgValueVec[DanglingDebugValue[Reg].second] = 0;
        DanglingDebugValue[Reg] = std::make_pair((MachineInstr*)0, 0);
      }

      std::vector<SUnit *> &UseList = Uses[Reg];
      std::vector<SUnit *> &DefList = Defs[Reg];
      // Optionally add output and anti dependencies. For anti
      // dependencies we use a latency of 0 because for a multi-issue
      // target we want to allow the defining instruction to issue
      // in the same cycle as the using instruction.
      // TODO: Using a latency of 1 here for output dependencies assumes
      //       there's no cost for reusing registers.
      SDep::Kind Kind = MO.isUse() ? SDep::Anti : SDep::Output;
      unsigned AOLatency = (Kind == SDep::Anti) ? 0 : 1;
      for (unsigned i = 0, e = DefList.size(); i != e; ++i) {
        SUnit *DefSU = DefList[i];
        if (DefSU == &ExitSU)
          continue;
        if (DefSU != SU &&
            (Kind != SDep::Output || !MO.isDead() ||
             !DefSU->getInstr()->registerDefIsDead(Reg)))
          DefSU->addPred(SDep(SU, Kind, AOLatency, /*Reg=*/Reg));
      }
      // GB: added 1 lines
      if (TRI->isPhysicalRegister(Reg)) {
        // GB: changed 12 lines
        for (const unsigned *Alias = TRI->getAliasSet(Reg); *Alias; ++Alias) {
          std::vector<SUnit *> &DefList = Defs[*Alias];
          for (unsigned i = 0, e = DefList.size(); i != e; ++i) {
            SUnit *DefSU = DefList[i];
            // if (DefSU == &ExitSU)
            //   continue;
            if (DefSU != SU &&
                (Kind != SDep::Output || !MO.isDead() ||
                 !DefSU->getInstr()->registerDefIsDead(*Alias)))
              DefSU->addPred(SDep(SU, Kind, AOLatency, /*Reg=*/ *Alias));
          }
        }
      // GB: added 1 lines
      }

      if (MO.isDef()) {
        // Add any data dependencies.
        unsigned DataLatency = SU->Latency;
        for (unsigned i = 0, e = UseList.size(); i != e; ++i) {
          SUnit *UseSU = UseList[i];
          if (UseSU == SU)
            continue;
          // GB: added 2 lines
          if (UseSU == &ExitSU)
            continue;
          unsigned LDataLatency = DataLatency;
          // Optionally add in a special extra latency for nodes that
          // feed addresses.
          // TODO: Do this for register aliases too.
          // TODO: Perhaps we should get rid of
          // SpecialAddressLatency and just move this into
          // adjustSchedDependency for the targets that care about it.
          if (SpecialAddressLatency != 0 && !UnitLatencies &&
              UseSU != &ExitSU) {
            MachineInstr *UseMI = UseSU->getInstr();
            const TargetInstrDesc &UseTID = UseMI->getDesc();
            int RegUseIndex = UseMI->findRegisterUseOperandIdx(Reg);
            assert(RegUseIndex >= 0 && "UseMI doesn's use register!");
            if (RegUseIndex >= 0 &&
                (UseTID.mayLoad() || UseTID.mayStore()) &&
                (unsigned)RegUseIndex < UseTID.getNumOperands() &&
                UseTID.OpInfo[RegUseIndex].isLookupPtrRegClass())
              LDataLatency += SpecialAddressLatency;
          }
          // Adjust the dependence latency using operand def/use
          // information (if any), and then allow the target to
          // perform its own adjustments.
          const SDep& dep = SDep(SU, SDep::Data, LDataLatency, Reg);
          if (!UnitLatencies) {
            ComputeOperandLatency(SU, UseSU, const_cast<SDep &>(dep));
            ST.adjustSchedDependency(SU, UseSU, const_cast<SDep &>(dep));
          }
          UseSU->addPred(dep);
        }
        // GB: added 1 lines
        if (TRI->isPhysicalRegister(Reg)) {
          // GB: changed 14 lines
          for (const unsigned *Alias = TRI->getAliasSet(Reg); *Alias; ++Alias) {
            std::vector<SUnit *> &UseList = Uses[*Alias];
            for (unsigned i = 0, e = UseList.size(); i != e; ++i) {
              SUnit *UseSU = UseList[i];
              if (UseSU == SU || UseSU == &ExitSU)
                continue;
              const SDep& dep = SDep(SU, SDep::Data, DataLatency, *Alias);
              if (!UnitLatencies) {
                ComputeOperandLatency(SU, UseSU, const_cast<SDep &>(dep));
                ST.adjustSchedDependency(SU, UseSU, const_cast<SDep &>(dep));
              }
              UseSU->addPred(dep);
            }
          }
        // GB: added 1 lines
        }

        // If a def is going to wrap back around to the top of the loop,
        // backschedule it.
        if (!UnitLatencies && DefList.empty()) {
          LoopDependencies::LoopDeps::iterator I = LoopRegs.Deps.find(Reg);
          if (I != LoopRegs.Deps.end()) {
            const MachineOperand *UseMO = I->second.first;
            unsigned Count = I->second.second;
            const MachineInstr *UseMI = UseMO->getParent();
            unsigned UseMOIdx = UseMO - &UseMI->getOperand(0);
            const TargetInstrDesc &UseTID = UseMI->getDesc();
            // TODO: If we knew the total depth of the region here, we could
            // handle the case where the whole loop is inside the region but
            // is large enough that the isScheduleHigh trick isn't needed.
            if (UseMOIdx < UseTID.getNumOperands()) {
              // Currently, we only support scheduling regions consisting of
              // single basic blocks. Check to see if the instruction is in
              // the same region by checking to see if it has the same parent.
              if (UseMI->getParent() != MI->getParent()) {
                unsigned Latency = SU->Latency;
                if (UseTID.OpInfo[UseMOIdx].isLookupPtrRegClass())
                  Latency += SpecialAddressLatency;
                // This is a wild guess as to the portion of the latency which
                // will be overlapped by work done outside the current
                // scheduling region.
                Latency -= std::min(Latency, Count);
                // Add the artifical edge.
                // GB: changed 4 lines
                // ExitSU.addPred(SDep(SU, SDep::Order, Latency,
                //                     /*Reg=*/0, /*isNormalMemory=*/false,
                //                     /*isMustAlias=*/false,
                //                     /*isArtificial=*/true));
              } else if (SpecialAddressLatency > 0 &&
                         UseTID.OpInfo[UseMOIdx].isLookupPtrRegClass()) {
                // The entire loop body is within the current scheduling region
                // and the latency of this operation is assumed to be greater
                // than the latency of the loop.
                // TODO: Recursively mark data-edge predecessors as
                //       isScheduleHigh too.
                SU->isScheduleHigh = true;
              }
            }
            LoopRegs.Deps.erase(I);
          }
        }

        UseList.clear();
        if (!MO.isDead())
          DefList.clear();
        DefList.push_back(SU);
      } else {
        UseList.push_back(SU);
      }
    }

    // Add chain dependencies.
    // Chain dependencies used to enforce memory order should have
    // latency of 0 (except for true dependency of Store followed by
    // aliased Load... we estimate that with a single cycle of latency
    // assuming the hardware will bypass)
    // Note that isStoreToStackSlot and isLoadFromStackSLot are not usable
    // after stack slots are lowered to actual addresses.
    // TODO: Use an AliasAnalysis and do real alias-analysis queries, and
    // produce more precise dependence information.
#define STORE_LOAD_LATENCY 1
    unsigned TrueMemOrderLatency = 0;
    if (TID.isCall() || MI->hasUnmodeledSideEffects() ||
        (MI->hasVolatileMemoryRef() &&
         (!TID.mayLoad() || !MI->isInvariantLoad(AA)))) {
      // Be conservative with these and add dependencies on all memory
      // references, even those that are known to not alias.
      for (std::map<const Value *, SUnit *>::iterator I =
             NonAliasMemDefs.begin(), E = NonAliasMemDefs.end(); I != E; ++I) {
        I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
      for (std::map<const Value *, std::vector<SUnit *> >::iterator I =
             NonAliasMemUses.begin(), E = NonAliasMemUses.end(); I != E; ++I) {
        for (unsigned i = 0, e = I->second.size(); i != e; ++i)
          I->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      }
      NonAliasMemDefs.clear();
      NonAliasMemUses.clear();
      // Add SU to the barrier chain.
      if (BarrierChain)
        BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      BarrierChain = SU;

      // fall-through
    new_alias_chain:
      // Chain all possibly aliasing memory references though SU.
      if (AliasChain)
        AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      AliasChain = SU;
      for (unsigned k = 0, m = PendingLoads.size(); k != m; ++k)
        PendingLoads[k]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      for (std::map<const Value *, SUnit *>::iterator I = AliasMemDefs.begin(),
           E = AliasMemDefs.end(); I != E; ++I) {
        I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
      for (std::map<const Value *, std::vector<SUnit *> >::iterator I =
           AliasMemUses.begin(), E = AliasMemUses.end(); I != E; ++I) {
        for (unsigned i = 0, e = I->second.size(); i != e; ++i)
          I->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      }
      PendingLoads.clear();
      AliasMemDefs.clear();
      AliasMemUses.clear();
    } else if (TID.mayStore()) {
      bool MayAlias = true;
      TrueMemOrderLatency = STORE_LOAD_LATENCY;
      if (const Value *V = getUnderlyingObjectForInstr(MI, MFI, MayAlias)) {
        // A store to a specific PseudoSourceValue. Add precise dependencies.
        // Record the def in MemDefs, first adding a dep if there is
        // an existing def.
        std::map<const Value *, SUnit *>::iterator I =
          ((MayAlias) ? AliasMemDefs.find(V) : NonAliasMemDefs.find(V));
        std::map<const Value *, SUnit *>::iterator IE =
          ((MayAlias) ? AliasMemDefs.end() : NonAliasMemDefs.end());
        if (I != IE) {
          I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0, /*Reg=*/0,
                                  /*isNormalMemory=*/true));
          I->second = SU;
        } else {
          if (MayAlias)
            AliasMemDefs[V] = SU;
          else
            NonAliasMemDefs[V] = SU;
        }
        // Handle the uses in MemUses, if there are any.
        std::map<const Value *, std::vector<SUnit *> >::iterator J =
          ((MayAlias) ? AliasMemUses.find(V) : NonAliasMemUses.find(V));
        std::map<const Value *, std::vector<SUnit *> >::iterator JE =
          ((MayAlias) ? AliasMemUses.end() : NonAliasMemUses.end());
        if (J != JE) {
          for (unsigned i = 0, e = J->second.size(); i != e; ++i)
            J->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency,
                                       /*Reg=*/0, /*isNormalMemory=*/true));
          J->second.clear();
        }
        if (MayAlias) {
          // Add dependencies from all the PendingLoads, i.e. loads
          // with no underlying object.
          for (unsigned k = 0, m = PendingLoads.size(); k != m; ++k)
            PendingLoads[k]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
          // Add dependence on alias chain, if needed.
          if (AliasChain)
            AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
        }
        // Add dependence on barrier chain, if needed.
        if (BarrierChain)
          BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      } else {
        // Treat all other stores conservatively.
        goto new_alias_chain;
      }

      // GB: changed 7 lines
      // if (!ExitSU.isPred(SU))
      //   // Push store's up a bit to avoid them getting in between cmp
      //   // and branches.
      //   ExitSU.addPred(SDep(SU, SDep::Order, 0,
      //                       /*Reg=*/0, /*isNormalMemory=*/false,
      //                       /*isMustAlias=*/false,
      //                       /*isArtificial=*/true));
    } else if (TID.mayLoad()) {
      bool MayAlias = true;
      TrueMemOrderLatency = 0;
      if (MI->isInvariantLoad(AA)) {
        // Invariant load, no chain dependencies needed!
      } else {
        if (const Value *V =
            getUnderlyingObjectForInstr(MI, MFI, MayAlias)) {
          // A load from a specific PseudoSourceValue. Add precise dependencies.
          std::map<const Value *, SUnit *>::iterator I =
            ((MayAlias) ? AliasMemDefs.find(V) : NonAliasMemDefs.find(V));
          std::map<const Value *, SUnit *>::iterator IE =
            ((MayAlias) ? AliasMemDefs.end() : NonAliasMemDefs.end());
          if (I != IE)
            I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0, /*Reg=*/0,
                                    /*isNormalMemory=*/true));
          if (MayAlias)
            AliasMemUses[V].push_back(SU);
          else
            NonAliasMemUses[V].push_back(SU);
        } else {
          // A load with no underlying object. Depend on all
          // potentially aliasing stores.
          for (std::map<const Value *, SUnit *>::iterator I =
                 AliasMemDefs.begin(), E = AliasMemDefs.end(); I != E; ++I)
            I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));

          PendingLoads.push_back(SU);
          MayAlias = true;
        }

        // Add dependencies on alias and barrier chains, if needed.
        if (MayAlias && AliasChain)
          AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
        if (BarrierChain)
          BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
    }
    // GB: added 8 lines
    // This "&& SU->Succs.empty()" is fishy. It prevents us from adding
    // barrier edges that should really be present, since the number of
    // successors isn't really relevant to whether the barrier is reachable
    // from one of those successors! However, this still seems to work this
    // way, so don't touch it.
    if (PreviousBarrier != NULL && SU->Succs.empty()) {
      PreviousBarrier->addPred(SDep(SU, SDep::Order, /* latency = */ 0));
    }
  }

  for (int i = 0, e = TRI->getNumRegs(); i != e; ++i) {
    Defs[i].clear();
    Uses[i].clear();
  }
  PendingLoads.clear();
}

/// ReleaseSucc - Decrement the NumPredsLeft count of a successor. Add it to
/// the PendingQueue if the count reaches zero. Also update its cycle bound.
void UAS::ReleaseSucc(SUnit *SU, SDep *SuccEdge) {
  SUnit *SuccSU = SuccEdge->getSUnit();

#ifndef NDEBUG
  if (SuccSU->NumPredsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    SuccSU->dump(this);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(0);
  }
#endif
  --SuccSU->NumPredsLeft;

  // Compute how many cycles it will be before this actually becomes
  // available.  This is the max of the start time of all predecessors plus
  // their latencies.
  SuccSU->setDepthToAtLeast(SU->getDepth() + SuccEdge->getLatency());

  // If all the node's predecessors are scheduled, this node is ready
  // to be scheduled. Ignore the special ExitSU node.
  if (SuccSU->NumPredsLeft == 0 && SuccSU != &ExitSU)
    PendingQueue.push_back(SuccSU);
}

/// ReleaseSuccessors - Call ReleaseSucc on each of SU's successors.
void UAS::ReleaseSuccessors(SUnit *SU) {
  for (SUnit::succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    ReleaseSucc(SU, &*I);
  }
}

/// ScheduleNodeTopDown - Add the node to the schedule. Decrement the pending
/// count of its successors. If a successor pending count is zero, add it to
/// the Available queue.
void UAS::ScheduleNodeTopDown(SUnit *SU, unsigned CurCycle, unsigned side) {
  DEBUG(dbgs() << "*** Scheduling [" << CurCycle << "]: ");
  DEBUG(SU->dump(this));
  // rewrite instruction if assigned to other cluster
  if (rewriteAssignedCluster(SU, side)) {
    MachineOperand &MO = SU->getInstr()->getOperand(0);
    if (MO.isReg() && MO.isDef()) {
      // need to change the register class
      MRI.setRegClass(MO.getReg(), sideToRC(side));
      CAState->addVChange(MO.getReg(), sideToRC(side));
    }
    DEBUG(dbgs() << "**** Cluster assignment: ");
    DEBUG(SU->dump(this));
  } else {
    // for some pseudo instructions (that are not assigned) we need to assign
    // the defined register
    rewriteDefs(SU);
  }

  rewriteUses(SU);

  /*
  MachineOperand &MO = SU->getInstr()->getOperand(0);
  if (MO.isReg() && MO.isDef() &&
      (MRI.getRegClass(MO.getReg()) == GPRegsRegisterClass)) {
    SU->dump(this);
    assert(false && "GPReg assignment");
  }
  */


  Sequence.push_back(SU);
  assert(CurCycle >= SU->getDepth() &&
         "Node scheduled above its depth!");
  SU->setDepthToAtLeast(CurCycle);

  ReleaseSuccessors(SU);
  SU->isScheduled = true;
  AvailableQueue.ScheduledNode(SU);
}


void UAS::Schedule() {
  BuildSchedGraph(NULL);

  DEBUG(dbgs() << "********** UAS Scheduling **********\n");
  DEBUG(for (unsigned su = 0, e = SUnits.size(); su != e; ++su)
          SUnits[su].dumpAll(this));

  AvailableQueue.initNodes(SUnits);
  ScheduleTopDown();
  AvailableQueue.releaseState();

  EmitSchedule();
}

void UAS::ScheduleTopDown() {
  unsigned CurCycle = 0;

  // Release any successors of the special Entry node.
  ReleaseSuccessors(&EntrySU);

  // Add all leaves to Available queue.
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    // It is available if it has no predecessors.
    bool available = SUnits[i].Preds.empty();
    if (available) {
      AvailableQueue.push(&SUnits[i]);
      SUnits[i].isAvailable = true;
    }
  }

  // When scheduling XCC, we make an implicit assignment decision for a later
  // cycle.
  std::map<SUnit*, ClusterPriority> preAssigned;
  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  std::vector<SUnit*> NotReady, WaitingForCopy;
  Sequence.reserve(SUnits.size());
  while (!AvailableQueue.empty() || !PendingQueue.empty()) {
    // Check to see if any of the pending instructions are ready to issue.  If
    // so, add them to the available queue.
    unsigned MinDepth = ~0u;
    for (unsigned i = 0, e = PendingQueue.size(); i != e; ++i) {
      if (PendingQueue[i]->getDepth() <= CurCycle) {
        AvailableQueue.push(PendingQueue[i]);
        PendingQueue[i]->isAvailable = true;
        PendingQueue[i] = PendingQueue.back();
        PendingQueue.pop_back();
        --i; --e;
      } else if (PendingQueue[i]->getDepth() < MinDepth)
        MinDepth = PendingQueue[i]->getDepth();
    }

    DEBUG(dbgs() << "\n*** Examining Available\n"; AvailableQueue.dump(this));

    SUnit *FoundSUnit = 0;
    ClusterPriority cp;

    while (!AvailableQueue.empty()) {
      SUnit *CurSUnit = AvailableQueue.pop();

      if (neverHazard(CurSUnit)) {
        FoundSUnit = CurSUnit;
        break;
      }

      std::map<SUnit*, ClusterPriority>::iterator found = preAssigned.find(CurSUnit);
      if (found != preAssigned.end())
        cp = found->second;
      else
        cp = getClusterPriority(CurSUnit);
      bool scheduled = false;
      bool copy_scheduled = false;
      bool xpath = false;
      while (scheduled == false && copy_scheduled == false && cp.hasMore()) {
        int c = cp.getNext();
        DEBUG(dbgs() << "**** Trying on cluster " << c << "\n");
        if (XccStall(CurSUnit, c, CurCycle))
          continue;

        SmallVector<unsigned, 2> regs, ops;
        if (needCopy(CurSUnit, c, regs, ops)) {
          DEBUG(dbgs() << "**** Need XCC (#: " << regs.size() << ")\n");
          if (UseXPath && ops.size() == 1 && supportsXPath(CurSUnit, ops[0]) &&
              FUSched->XPathOnSideAvailable(c)) {
            DEBUG(dbgs() << "**** Instruction can use XPath\n");
            xpath = true;
            // XXX load/store ?
          } else {
            copy_scheduled = FUSched->ScheduleXCC(c);
            if (copy_scheduled) {
              // insert COPY instructions
              for (int i = 0, e = regs.size(); i < e; ++i) {
                unsigned newreg = insertCopy(regs[i], c);
                CurSUnit->getInstr()->getOperand(ops[i]).setReg(newreg);
              }
              preAssigned[CurSUnit] = ClusterPriority(c);
            }
            break;
          }
        }

        scheduled = FUSched->ScheduleOnSide(CurSUnit, c);
      }

      if (scheduled) {
        FoundSUnit = CurSUnit;
        if (xpath) FUSched->setXPath(CurSUnit->getInstr(), true);
        break;
      }

      if (copy_scheduled)
        WaitingForCopy.push_back(CurSUnit);
      else
        NotReady.push_back(CurSUnit);
    }

    // Add the nodes that aren't ready back onto the available list.
    if (NotReady.size() || WaitingForCopy.size()) {
      AvailableQueue.push_all(NotReady);
      AvailableQueue.push_all(WaitingForCopy);
      NotReady.clear();
      WaitingForCopy.clear();
    }

    // If we found a node to schedule...
    if (FoundSUnit) {
      // node was scheduled on an FU, now put it in the sequence
      ScheduleNodeTopDown(FoundSUnit, CurCycle, cp.getLast());
    } else {
      DEBUG(dbgs() << "*** Finished cycle " << CurCycle << '\n');
      FUSched->AdvanceCycle();
      ++CurCycle;
    }
  }
}

bool UAS::neverHazard(SUnit *SU) {
  switch (SU->getInstr()->getOpcode()) {
  case TargetOpcode::COPY:
  {
    // XXX copies need to be assigned too!
    unsigned srcReg = SU->getInstr()->getOperand(1).getReg();
    unsigned dstReg = SU->getInstr()->getOperand(0).getReg();
    if (TRI->isVirtualRegister(dstReg)) {
      if (TRI->isVirtualRegister(srcReg))
        // vreg -> vreg copies do not get assigned (eg. aReg -> predReg)
        return true;

      const TargetRegisterClass *RC = ARegsRegisterClass;
      MRI.setRegClass(dstReg, RC);
      DEBUG(dbgs() << "**** RC assigned [" << RC->getName() << "]: ");
      DEBUG(SU->dump(this));
    }
    return true;
  }
  }
  return false;
}

bool UAS::XccStall(SUnit *SU, unsigned side, unsigned CurCycle) {
  for (SUnit::pred_iterator I = SU->Preds.begin(), E = SU->Preds.end();
       I != E; ++I) {
    SUnit *PredSU = I->getSUnit();
    // pre-conditions for an XPATH stall:
    // 1. it's a true dependency
    // 2. predecessor has latency, ie. it's a real instruction
    // 3. dest cluster is different from the one of the predecessor
    // 4. predecessor is not a load
    if (I->getKind() == SDep::Data && I->getLatency() &&
        side != getAssignedSide(PredSU) &&
        !(PredSU->getInstr()->getDesc().TSFlags & TMS320C64XII::is_memaccess))
    {
      int slack = CurCycle - (PredSU->getDepth() + PredSU->Latency);
      if (slack == 0) {
        DEBUG(dbgs() << "**** XCC Stall: SU(" << SU->NodeNum << ") --> SU("
              << PredSU->NodeNum << ") = " << slack << "\n");
        ++XccStalls;
        return true;
      }
    }
  }
  return false;
}

unsigned UAS::getAssignedSide(SUnit *SU) {
  // we rewrite cluster sides during assignment
  return TMS320C64XInstrInfo::getSide(SU->getInstr());
}

unsigned UAS::getVRegSide(unsigned reg) {
  // look up whether vreg has been remapped
  const TargetRegisterClass *RC = MRI.getRegClass(reg);
  if (RC == ARegsRegisterClass || RC->hasSuperClass(ARegsRegisterClass))
    return 0; // ASide
  else if (RC == BRegsRegisterClass || RC->hasSuperClass(BRegsRegisterClass))
    return 1; // BSide

  DEBUG(dbgs() << "Cannot determine cluster for reg in class: "
        << RC->getName() << "\n");
  DEBUG(dbgs() << "live-in: " << MRI.isLiveIn(reg) << "\n");
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (MachineRegisterInfo::livein_iterator LI = MRI.livein_begin(),
       LE = MRI.livein_end(); LI != LE; ++LI) {
    DEBUG(dbgs() << PrintReg(LI->second) << " -> arg-"
          << PrintReg(LI->first, TRI) << "\n");
  }

  llvm_unreachable("reg side mishap");
  return -1;
}

unsigned UAS::getVRegOnSide(unsigned reg, unsigned side) {
  // reg on requested side, return original
  if (getVRegSide(reg) == side)
    return reg;
  // otherwise check for a copy
  return CAState->getXccVReg(reg, side);
}

bool UAS::needCopy(SUnit *SU, unsigned side, SmallVector<unsigned,2> &regs,
                   SmallVector<unsigned,2> &ops) {
  const MachineInstr *MI = SU->getInstr();
  const TargetInstrDesc desc = MI->getDesc();

  // don't require XCC for COPIES
  if (MI->getOpcode() == TargetOpcode::COPY)
    return false;

  for (unsigned i = 0, e = std::max(
      MI->findFirstPredOperandIdx(), (int) desc.NumOperands); i < e; ++i) {
    const MachineOperand &op = MI->getOperand(i);
    if (op.isReg() && op.isUse()) {
      int reg = op.getReg();
      if (TargetRegisterInfo::isVirtualRegister(reg)) {
        const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
        const TargetRegisterClass *OpRC = TOI.getRegClass(TRI);

        // Predicate register can be used from both cluster sides.
        if (OpRC == PredRegsRegisterClass)
          continue;

        const TargetRegisterClass *RegRC = MRI.getRegClass(reg);

        // XXX most likely from a COPY, which still needs to be handled
        if (RegRC == GPRegsRegisterClass)
          continue;

        // XXX when the vreg is a pred reg, we don't know its side and need a
        // copy to be safe
        if (RegRC == PredRegsRegisterClass) {
          if (CAState->getXccVReg(reg, side))
            continue; // copy is already there
          else {
            regs.push_back(reg);
            ops.push_back(i);
          }
        // is the register available on this side?
        } else if (!getVRegOnSide(reg, side)) {
          regs.push_back(reg); // we need to copy it
          ops.push_back(i);
        }
      }
    }
  }
  return regs.size();
}

unsigned UAS::insertCopy(unsigned reg, unsigned side) {
  unsigned vnew = MRI.createVirtualRegister(sideToRC(side));
  DebugLoc dl;
  MachineInstr *MI =
    BuildMI(MF, dl, TII->get(TargetOpcode::COPY), vnew).addReg(reg);
  SUnit *SU = new SUnit(MI, SUnits.size() + CopySUs.size());
  SU->OrigNode = SU;
  CopySUs.push_back(SU);
  Sequence.push_back(SU);
  CAState->addXccSplit(reg, vnew, side, MI);
  DEBUG(dbgs() << "*** Scheduling [XCC]: ");
  DEBUG(SU->dump(this));
  XccInserted++;
  return vnew;
}

bool UAS::supportsXPath(SUnit *SU, unsigned opNum) {
  const MachineInstr *MI = SU->getInstr();
  const TargetOperandInfo &TOI = MI->getDesc().OpInfo[opNum];
  const TargetRegisterClass *RC = TOI.getRegClass(TRI);
  return (RC == GPRegsRegisterClass);
}

const TargetRegisterClass *UAS::sideToRC(unsigned side) const {
  if (side == 0)
    return ARegsRegisterClass;
  else
    return BRegsRegisterClass;
}

bool UAS::rewriteAssignedCluster(SUnit *SU, unsigned side) {
  const TMS320C64XInstrInfo *tii = static_cast<const TMS320C64XInstrInfo*>(TII);
  MachineInstr *MI = SU->getInstr();

  if (!TMS320C64XInstrInfo::isFlexible(MI))
    return false;

  int newOpc = tii->getSideOpcode(MI->getOpcode(), side);
  if (newOpc != MI->getOpcode()) {
    const TargetInstrDesc &desc = TII->get(newOpc);
    MI->setDesc(desc);
    return true;
  }
  return false;
}

void UAS::rewriteDefs(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();
  MachineOperand &MO = MI->getOperand(0);
  if (!MO.isReg() || !MO.isDef()
      || !TargetRegisterInfo::isVirtualRegister(MO.getReg())
      || MRI.getRegClass(MO.getReg()) != GPRegsRegisterClass)
    return;

  if (MI->getOpcode() == COPY) {
    MachineOperand &src = SU->getInstr()->getOperand(1);
    assert(MRI.getRegClass(src.getReg()) != GPRegsRegisterClass);
    MRI.setRegClass(MO.getReg(), MRI.getRegClass(src.getReg()));
    CAState->addVChange(MO.getReg(), MRI.getRegClass(src.getReg()));
    return;
  }

  MRI.setRegClass(MO.getReg(), ARegsRegisterClass);
  CAState->addVChange(MO.getReg(), ARegsRegisterClass);
}

void UAS::rewriteUses(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();
  const TargetInstrDesc desc = MI->getDesc();
  for (unsigned i = 0, e = std::min((unsigned) desc.NumOperands, MI->getNumOperands());
       i != e; ++i) {
    const TargetOperandInfo &TOI = desc.OpInfo[i];
    MachineOperand &MO = MI->getOperand(i);

    if (MO.isReg() && MO.isUse()) {
      unsigned reg = MO.getReg();

      // the register class required by the instruction
      const TargetRegisterClass *RCrequired = TOI.getRegClass(TRI);
      // may not have register class associated (eg. COPY)
      if (!RCrequired)
        continue;

      // predicate registers can be read from both sides
      if (RCrequired == PredRegsRegisterClass)
        continue;

      const TargetRegisterClass *RCreg;
      if (TargetRegisterInfo::isVirtualRegister(reg))
        RCreg = MRI.getRegClass(reg);
      else
        RCreg = TMS320C64X::BRegsRegClass.contains(reg) ? BRegsRegisterClass :
          ARegsRegisterClass;

      if ((RCrequired != RCreg) && !RCrequired->hasSubClass(RCreg)) {
        DEBUG(dbgs() << "RCs disagree at operand " << MO
              << ", req: " << RCrequired->getName()
              << ", reg: " << RCreg->getName() << "\n");
        unsigned newreg = CAState->getXccVReg(reg, RCrequired);
        assert(newreg);
        MO.setReg(newreg);
        DEBUG(dbgs() << "**** Operand rewrite: ");
        DEBUG(SU->dump(this));
      }
    }
  }
}


ClusterPriority UAS::getClusterPriority(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();

  // only one cluster available when we cannot assign
  if (!TMS320C64XInstrInfo::isFlexible(MI)) {
    return ClusterPriority(TMS320C64XInstrInfo::getSide(MI));
  }

  switch (PF) {
  case None:
    return ClusterPriority(0, 1);
  case Random:
    return prioRandom(MI);
  case MWP:
  default:
    return prioMWP(MI);
  }
}

ClusterPriority UAS::prioRandom(MachineInstr *MI) {
  if (rand_r(&RandState) % 2)
    return ClusterPriority(0, 1);
  else
    return ClusterPriority(1, 0);
}

ClusterPriority UAS::prioMWP(MachineInstr *MI) {
  std::pair<int,int> opcnt = countOperandSides(MI, MRI);
  if (opcnt.second > opcnt.first) {
    return ClusterPriority(1, 0);
  } else {
    return ClusterPriority(0, 1);
  }
}

std::pair<int,int>
TMS320C64X::countOperandSides(const MachineInstr *MI,
                              MachineRegisterInfo &MRI) {
  int opcntA = 0, opcntB = 0;
  for (int i = 1,
       e = std::min(MI->getNumOperands(), (unsigned) MI->getDesc().NumOperands);
       i < e; ++i) {
    const TargetOperandInfo &TOI = MI->getDesc().OpInfo[i];
    if (TOI.isPredicate())
      continue;
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    const TargetRegisterClass *sideRC = MRI.getRegClass(MO.getReg());
    if (sideRC == ARegsRegisterClass)
      opcntA++;
    else if (sideRC == BRegsRegisterClass)
      opcntB++;
  }
  return std::make_pair(opcntA, opcntB);
}

ClusterDAG *TMS320C64X::createClusterDAG(AssignmentAlgorithm AA,
                                         MachineFunction &MF,
                                         const MachineLoopInfo &MLI,
                                         const MachineDominatorTree &MDT,
                                         AssignmentState *state) {
  // everything is UAS right now
  UAS *uas = new UAS(MF, MLI, MDT, state);

  switch(AA) {
  case ClusterUAS:
    uas->setPriority(UAS::MWP); // default
    break;
  case ClusterUAS_none:
    uas->setPriority(UAS::None);
    break;
  case ClusterUAS_rand:
    uas->setPriority(UAS::Random);
    break;
  default:
    llvm_unreachable("not a DAG based algorithm");
  }

  return uas;
}

