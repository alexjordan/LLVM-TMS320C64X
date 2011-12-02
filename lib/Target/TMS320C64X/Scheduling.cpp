//===-- TMS320C64X/Scheduling.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// BuildSchedGraph implementation
//
//===----------------------------------------------------------------------===//

#include "Scheduling.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetSubtarget.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

//#undef DEBUG
//#define DEBUG(x) x
using namespace llvm;
using namespace TMS320C64X;

void TMS320C64X::SchedulerBase::BuildSchedGraph(AliasAnalysis *AA) {
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

    // The Regs used by SU
    SmallSet<unsigned, 8> CurrentUses;

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
        // AJO the uses have been cleared, but uses by the current SU need to be
        // restored (eg. calls may use some of the registers that are clobbered
        // by the callee (imp-def))
        if (CurrentUses.count(Reg))
          UseList.push_back(SU);

        if (!MO.isDead())
          DefList.clear();
        DefList.push_back(SU);
      } else {
        UseList.push_back(SU);
        CurrentUses.insert(Reg);
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
          // XXX
          // XXX aliasing stores must not be issued in the same cycle (hack
          // XXX for TMS320C64X).
          // XXX
          I->second->addPred(SDep(SU, SDep::Order,
                                  /*Latency=*/TrueMemOrderLatency,
                                  /*Reg=*/0, /*isNormalMemory=*/true));
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

template <class ForwardIter>
void TMS320C64X::SchedulerBase::BuildSchedGraph(ForwardIter first,
                                                ForwardIter last,
                                                AliasAnalysis *AA) {

  // Most of this code is copied from ScheduleDAGInstrs::BuildSchedGraph. We
  // needed to make small changes; this version is supposed to build a
  // single graph for the whole block under consideration. We went for a
  // modified copy of that code since modifying it directly would break the
  // postpass scheduler.

  // We'll be allocating one SUnit for each instruction, plus one for
  // the region exit node.
  SUnits.reserve(std::distance(first, last));

  // We build scheduling units by walking a block's instruction list from bottom
  // to top.

  // Remember where a generic side-effecting instruction is as we procede.
  SUnit *BarrierChain = 0, *AliasChain = 0;

  // Used to order side-effecting instructions within the region (ie. between
  // side exits).
  // XXX consider rewrite and use of the barrier chain for this
  SUnit *SideEffectChain = 0;

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
  // AJO: don't bother yet
  //AddSchedBarrierDeps();

  // Walk the list of instructions, from bottom moving up.
  // GB: added 1 line
  SUnit *PreviousBarrier = NULL;
  for (ForwardIter MII = first, MIE = last; MII != MIE; ++MII) {
    MachineInstr *MI = *MII;

    // XXX hack for PHIs until reverse iterator has been fixed
    if (MI->isPHI())
      continue;

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
        TII->isSchedulingBoundary(MI, MI->getParent(), MF)) {
      SUnit *BarrierSU = NewSUnit(MI);
      BarrierSU->Latency = (UnitLatencies ? 1 : 0);
      SUnit *I = (PreviousBarrier ? PreviousBarrier : &SUnits.front());
      while (I != BarrierSU) {
        if (I->isCall)
          assert(false && "call may need chain dep");
        I++;
      }
      // keep side-effect instructions from crossing barriers
      // XXX use latency of the branch?
      if (SideEffectChain)
        SideEffectChain->addPred(SDep(BarrierSU, SDep::Order, 1));
      if (PreviousBarrier)
        PreviousBarrier->addPred(SDep(BarrierSU, SDep::Order, 0));
      PreviousBarrier = BarrierSU;
      SideEffectChain = BarrierSU;
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

      if (SideEffectChain)
        SideEffectChain->addPred(SDep(SU, SDep::Order, /*Latency=*/1));
      SideEffectChain = SU;

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

      if (SideEffectChain)
        SideEffectChain->addPred(SDep(SU, SDep::Order, /*Latency=*/1));
      SideEffectChain = SU;

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
        if (SideEffectChain)
          SideEffectChain->addPred(SDep(SU, SDep::Order, /*Latency=*/1));
        SideEffectChain = SU;

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

bool SchedulerBase::hasSideEffects(const MachineInstr *MI) {
  const TargetInstrDesc &TID = MI->getDesc();
  return (TID.mayLoad() || TID.mayStore() || TID.hasUnmodeledSideEffects());
}

void TMS320C64X::RegionScheduler::BuildSchedGraph(AliasAnalysis *AA) {
  MachineSingleEntryPathRegion::instr_reverse_iterator begin, end;
  begin = MR->instr_rbegin();
  end = MR->instr_rend();

  SchedulerBase::BuildSchedGraph(begin, end, AA);
}

bool RegionScheduler::isBeingScheduled(MachineBasicBlock *MBB) const {
  return MR->contains(MBB);
}

void TMS320C64X::RegionScheduler::Run(MachineSingleEntryPathRegion *R) {
  // eliminate fallthroughs within region
  preprocess(R);

  MachineSingleEntryPathRegion::instr_reverse_iterator begin, end;
  begin = R->instr_rbegin();
  end = R->instr_rend();

  dbgs() << "Scheduling region (" << R->size() << " block(s))\n";
  for (MachineSingleEntryPathRegion::instr_iterator I = R->instr_begin(),
       E = R->instr_end(); I != E; ++I) {
    (*I)->dump();
  }
#if 0
  dbgs() << "Reversed region\n";
  for (MachineSingleEntryPathRegion::instr_reverse_iterator I = R->instr_rbegin(),
       E = R->instr_rend(); I != E; ++I) {
    (*I)->dump();
  }
#endif
  MR = R;
  SUnits.clear();
  Sequence.clear();
  EntrySU = SUnit();
  ExitSU = SUnit();

  Schedule();

  DEBUG({
      dbgs() << "*** Final schedule ***\n";
      dumpSchedule();
      dbgs() << '\n';
    });
}

void RegionScheduler::preprocess(MachineSingleEntryPathRegion *MR) const {
  // not neccessary when region is trivial
  if (MR->size() < 2)
    return;

  for (MachineRegion::iterator I = MR->begin(), E = MR->end(); I != E; ++I) {
    MachineBasicBlock *MBB = *I;
    if (MBB->getFirstTerminator() == MBB->end()) {
      assert(!MBB->succ_empty());
      DEBUG(dbgs() << "inserted branch for fallthrough (" << MBB->getName()
            << ")\n");
      TII->InsertBranch(*MBB, *MBB->succ_begin(), NULL,
                        SmallVector<MachineOperand,0>(),
                        DebugLoc());
    }
  }
}

MachineInstr *removeInstrs(MachineBasicBlock *MBB,
                           MachineBasicBlock::iterator first,
                           MachineBasicBlock::iterator last) {
  MachineInstr *lastTerm = NULL;
  for (MachineBasicBlock::iterator I = first, E = last;
       I != E;) {
    if (I->getDesc().isTerminator())
      lastTerm = I;
    MBB->remove(I++);
  }
  return lastTerm;
}

template<class SeqIter>
SeqIter pushInstrs(MachineBasicBlock *MBB, MachineInstr* terminator,
                SeqIter first, SeqIter last) {

  for (; first != last; ++first) {
    SUnit *SU = *first;
    assert(SU);
    MachineInstr *MI = SU->getInstr();

    // to maintain MBB consistency, branch_cond and branch instructions must be
    // contiguous at the end of the block.
    MachineBasicBlock::iterator insertIt = MBB->getFirstTerminator();
    if (!MI->getDesc().isTerminator() && insertIt != MBB->end())
      // insert non-terminators before the first terminator
      MBB->insert(insertIt, SU->getInstr());
    else
      MBB->push_back(SU->getInstr());

    assert(!SU->DbgInstrList.size());
    if (SU->getInstr() == terminator) {
      ++first;
      break;
    }
  }
  return first;
}


MachineBasicBlock *TMS320C64X::RegionScheduler::EmitSchedule() {
  assert(!DbgValueVec.size() && "cannot emit dbg values to region");

  MachineSingleEntryRegion::iterator MBBI = MR->begin(), MBBE = MR->end();
  std::vector<SUnit*>::iterator SeqI = Sequence.begin(), SeqE = Sequence.end();
  std::list<MachineInstr*> terms;

  // remove instructions from blocks, remember their last terminator
  // begin with entry block
  MachineBasicBlock *MBB = *MBBI++;
  MachineInstr *terminator = removeInstrs(MBB, MBB->getFirstNonPHI(), MBB->end());
  terms.push_back(terminator);

  // remaining blocks
  while (MBBI != MBBE) {
    MBB = *MBBI++;
    assert(!MBB->begin()->isPHI() && "PHI in non-entry block");
    MachineInstr *terminator = removeInstrs(MBB, MBB->begin(), MBB->end());
    //assert((terminator || MBBI == MBBE) && "no term and not last block?");
    terms.push_back(terminator);
  }

  // while we have more instructions in the sequence, insert the instructions
  // into blocks again
  MBBI = MR->begin();
  while (SeqI != SeqE) {
    assert(MBBI != MBBE);
    MBB = *MBBI++;

    assert(terms.size());
    terminator = terms.front();
    terms.pop_front();

    SeqI = pushInstrs(MBB, terminator, SeqI, SeqE);
  }

  return const_cast<MachineBasicBlock*>(MR->getEntry());
}
