//===- TMS319C64XInstrInfo.cpp - TMS320C64X Instruction Information -------===//
//
// Copyright 2010 Jeremy Morse <jeremy.morse@gmail.com>. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY JEREMY MORSE ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL JEREMY MORSE OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//===----------------------------------------------------------------------===//

#include "TMS320C64X.h"
#include "TMS320C64XInstrInfo.h"
#include "TMS320C64XSubtarget.h"
#include "TMS320C64XGenInstrInfo.inc"

#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

namespace C64X = llvm::TMS320C64X;
using namespace llvm;

//-----------------------------------------------------------------------------
// Data for unit-to- string conversion
static const char *unit_str[] = { "L", "S", "M", "D" };
TMS320C64XInstrInfo::UnitStrings_t
  TMS320C64XInstrInfo::UnitStrings(unit_str, unit_str + 4);

//-----------------------------------------------------------------------------
// Data for resource-to-string conversion
// Note: resource is unit+side
std::string TMS320C64XInstrInfo::Res_a[] = { "L1", "S1", "M1", "D1" };
std::string TMS320C64XInstrInfo::Res_b[] = { "L2", "S2", "M2", "D2" };

//-----------------------------------------------------------------------------

TMS320C64XInstrInfo::TMS320C64XInstrInfo()
: TargetInstrInfoImpl(TMS320C64XInsts, array_lengthof(TMS320C64XInsts)),
  RI(*this)
{
  static const unsigned PseudoOpTbl[][3] = {
     { C64X::add_p_rr,    C64X::add_rr_1,   C64X::add_rr_2 }
    ,{ C64X::add_p_ri,    C64X::add_ri_1,   C64X::add_ri_2 }
    ,{ C64X::srl_p_rr,    C64X::srl_rr_1,   C64X::srl_rr_2 }
    ,{ C64X::srl_p_ri,    C64X::srl_ri_1,   C64X::srl_ri_2 }
    ,{ C64X::mpy32_p,     C64X::mpy32_1,    C64X::mpy32_2  }
    // address loads (_sload = data path and FU on the same side)
    //,{ C64X::word_load_p_addr,   C64X::word_load_1,    C64X::word_load_2}
    ,{ C64X::word_load_p_addr,   C64X::word_sload_1,   C64X::word_sload_2}
    ,{ C64X::word_store_p_addr,  C64X::word_store_1,   C64X::word_store_2}
    ,{ C64X::hword_load_p_addr,  C64X::hword_sload_1,  C64X::hword_sload_2}
    ,{ C64X::hword_store_p_addr, C64X::hword_store_1,  C64X::hword_store_2}
    ,{ C64X::uhword_load_p_addr, C64X::uhword_sload_1, C64X::uhword_sload_2}
    ,{ C64X::byte_load_p_addr,   C64X::byte_sload_1,   C64X::byte_sload_2}
    ,{ C64X::byte_store_p_addr,  C64X::byte_store_1,   C64X::byte_store_2}
    ,{ C64X::ubyte_load_p_addr,  C64X::ubyte_sload_1,  C64X::ubyte_sload_2}
    // index loads
    ,{ C64X::word_load_p_idx,    C64X::word_sload_1,  C64X::word_sload_2}
    ,{ C64X::word_store_p_idx,   C64X::word_store_1,  C64X::word_store_2}
    ,{ C64X::hword_load_p_idx,   C64X::hword_sload_1, C64X::hword_sload_2}
    ,{ C64X::hword_store_p_idx,  C64X::hword_store_1, C64X::hword_store_2}
    ,{ C64X::uhword_load_p_idx,  C64X::uhword_sload_1,C64X::uhword_sload_2}
    ,{ C64X::byte_load_p_idx,    C64X::byte_sload_1,  C64X::byte_sload_2}
    ,{ C64X::byte_store_p_idx,   C64X::byte_store_1,  C64X::byte_store_2}
    ,{ C64X::ubyte_load_p_idx,   C64X::ubyte_sload_1, C64X::ubyte_sload_2}
  };

  for (unsigned i = 0, e = array_lengthof(PseudoOpTbl); i != e; ++i) {
    unsigned PseudoOp = PseudoOpTbl[i][0];
    unsigned SideAOp = PseudoOpTbl[i][1];
    unsigned SideBOp = PseudoOpTbl[i][2];
    if (!Pseudo2ClusteredTable.insert(std::make_pair((unsigned*)PseudoOp,
            std::make_pair(SideAOp, SideBOp))).second)
      assert(false && "Duplicated entries?");
  }

#if 0
  // used for debugging instruction flags
  for (unsigned i = TMS320C64X::BUNDLE_END + 1 ;
      i < TMS320C64X::INSTRUCTION_LIST_END; ++i) {
    const TargetInstrDesc &desc = get(i);
    dbgs() << "-- TSFlags for " << desc.Name << "---\n";
    dumpFlags(get(i), dbgs());
    dbgs() << "-- END FLAGS ---------------\n";
  }
#endif
}

//-----------------------------------------------------------------------------

ScheduleHazardRecognizer *
TMS320C64XInstrInfo::CreateTargetHazardRecognizer(const TargetMachine *TM,
                                                  const ScheduleDAG *DAG) const
{
  const TargetInstrInfo *TII = TM->getInstrInfo();
  assert(TII && "No InstrInfo? Can not create a hazard recognizer!");

  // XXX hazard recognizer only works for post RA scheduler right now
  // return new TMS320C64XHazardRecognizer(*TII);

  // Otherwise use LLVM default
  return TargetInstrInfoImpl::CreateTargetHazardRecognizer(TM, DAG);
}

//-----------------------------------------------------------------------------

ScheduleHazardRecognizer*
TMS320C64XInstrInfo::CreatePostRAHazardRecognizer(const TargetMachine *TM)
{
  const TargetInstrInfo *TII = TM->getInstrInfo();
  assert(TII && "No InstrInfo? Can not create a hazard recognizer!");

  return new TMS320C64XHazardRecognizer(*TII);
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::getImmPredValue(const MachineInstr &MI) const {

  const int predIndex = MI.findFirstPredOperandIdx();

  // no predicates found for the MI
  if (predIndex == -1) return false;

  // MI is predicated and the predicate (immediate) is valid,
  // NOTE, we are not able to handle non-immediate predicates
  if (MI.getOperand(predIndex).getImm() != -1) return true;

  return false;
}

//-----------------------------------------------------------------------------

void TMS320C64XInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      DebugLoc DL,
                                      unsigned DestReg,
                                      unsigned SrcReg,
                                      bool KillSrc) const
{
  const bool destGPR = TMS320C64X::GPRegsRegClass.contains(DestReg);
  const bool srcGPR = TMS320C64X::GPRegsRegClass.contains(SrcReg);

  // we do not yet restrict register-moves between different sides
  // and therefore allow moves between arbitrary gen-purpose regs

  if (destGPR && srcGPR) {
    MachineInstrBuilder MIB =
      BuildMI(MBB, I, DL, get(TMS320C64X::mv), DestReg);

    MIB.addReg(SrcReg, getKillRegState(KillSrc));
    addDefaultPred(MIB);
    return;
  }

  llvm_unreachable("Can not copy physical registers!");
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::copyRegToReg(MachineBasicBlock &MBB,
				  MachineBasicBlock::iterator MI,
				  unsigned dstReg,
                                  unsigned srcReg,
				  const TargetRegisterClass*,
				  const TargetRegisterClass*) const
{
  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  addDefaultPred(BuildMI(MBB, MI, DL, get(TMS320C64X::mv))
    .addReg(dstReg, RegState::Define).addReg(srcReg));

  return true;
}

//-----------------------------------------------------------------------------

void
TMS320C64XInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         unsigned srcReg,
                                         bool isKill,
                                         int frameIndex,
                                         const TargetRegisterClass* RC,
                                         const TargetRegisterInfo* TRI) const
{
  DebugLoc DL;
  if  (MI != MBB.end()) DL = MI->getDebugLoc();

  const TargetRegisterClass *rc;
  if (!TargetRegisterInfo::isPhysicalRegister(srcReg)) {
    assert(RC);
    // XXX currently PredRegs don't allocate any B register, but this will not
    // hold forever, PredRegs probably need to be split into A/B.
    if (RC == TMS320C64X::PredRegsRegisterClass)
      rc = TMS320C64X::ARegsRegisterClass;
    else
      rc = RC;
  } else
    rc = findRegisterSide(srcReg, MBB.getParent());

  // the address is in A15, if the data is on B side, use T2
  bool xdata = (rc == TMS320C64X::BRegsRegisterClass);

  addFormOp(
    addDefaultPred(BuildMI(MBB, MI, DL, get(TMS320C64X::word_store_1))
      .addReg(TMS320C64X::A15).addFrameIndex(frameIndex)
      .addReg(srcReg, getKillRegState(isKill))),
    TMS320C64XII::unit_d, xdata);
}

//-----------------------------------------------------------------------------

void
TMS320C64XInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI,
                                          unsigned dstReg,
                                          int frameIndex,
                                          const TargetRegisterClass* RC,
                                          const TargetRegisterInfo* TRI) const
{
  DebugLoc DL;
  if  (MI != MBB.end()) DL = MI->getDebugLoc();

  const TargetRegisterClass *rc;
  if (!TargetRegisterInfo::isPhysicalRegister(dstReg)) {
    assert(RC);
    // XXX see above
    if (RC == TMS320C64X::PredRegsRegisterClass)
      rc = TMS320C64X::ARegsRegisterClass;
    else
      rc = RC;
  } else
    rc = findRegisterSide(dstReg, MBB.getParent());

  // the address is in A15, if the data is on B side, use T2
  bool xdata = (rc == TMS320C64X::BRegsRegisterClass);

  addFormOp(
    addDefaultPred(BuildMI(MBB, MI, DL, get(TMS320C64X::word_load_1))
      .addReg(dstReg, RegState::Define)
      .addReg(TMS320C64X::A15).addFrameIndex(frameIndex)),
    TMS320C64XII::unit_d, xdata);
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
		                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const
{
  // empty machine basic block, nothing to do
  if (MBB.begin() == MBB.end()) return false;

  MachineBasicBlock::iterator I = MBB.end();

  // if the machine basic block contains only debug
  // values, there is obviously nothing to analyze
  while((--I)->isDebugValue())
    if (I == MBB.begin()) return false;

  MachineInstr *LastInstr = I;

  // the machine basic block falls through (no branches)
  if (!(LastInstr->getDesc().isBranch())) return false;

  /// here we can be sure to be dealing with a regular branch-instruction.
  /// NOTE, we do not consider a return to be a branch ! Now to be able to
  /// analyze the branch properly, we additionally need to check whether
  /// the branch is conditional/predicated

  const int lastOpcode = LastInstr->getOpcode();
  const bool lastIsCond = lastOpcode == TMS320C64X::branch_cond;

  // there is only one branch instruction in the basic block
  if (I == MBB.begin() || !isUnpredicatedTerminator(--I)) {

    if (!lastIsCond) {

      // the destination is not available for indirect register branches,
      // therefore we can not handle them and skip the branch analysis
      if (lastOpcode == TMS320C64X::branch_reg) return true;

      FBB = 0; TBB = LastInstr->getOperand(0).getMBB();

      // here we seem to be dealing with an unconditional branch. If the
      // destination block is the layout-follower, we can remove the branch
      // since we are actually falling through
      if (MBB.isLayoutSuccessor(TBB) && AllowModify) {
        LastInstr->eraseFromParent();
        TBB = 0;
      }
    }
    else {

      // if the last branch instruction is a conditional branch, we have
      // to specify the outcomes of the branch and specify the condition.
      // The branch folder will then do the rest for us. Note, we do not
      // have a predication for a conditional branch, which would lead to
      // a situation of having a conditional conditional branch...

      if (TBB) FBB = TBB;
      TBB = LastInstr->getOperand(0).getMBB();

      // store the conditions of the conditional jump
      Cond.push_back(LastInstr->getOperand(1));  // Zero/NZ
      Cond.push_back(LastInstr->getOperand(2));  // Reg
    }
    return false;
  }

  MachineInstr *SecLastInstr = I;

  // if there are 3 terminators, we don't know what sort of bb this is
  if (SecLastInstr && I != MBB.begin() && isUnpredicatedTerminator(--I))
    return true;

  const int secLastOpcode = SecLastInstr->getOpcode();
  const bool secLastIsCond = secLastOpcode == TMS320C64X::branch_cond;

  if (!secLastIsCond) {

    // for now we can not handle indirect register branches
    if (secLastOpcode == TMS320C64X::branch_reg) return true;

    /// guarded by the immediate predicate which tells us the branch will be
    /// executed, in this case we can drop the very last instruction, since
    /// it is actually dead. Also, if the second last instruction branches to
    /// a follow block we can remove it as well, even if such a scenario is
    /// rare and actually shouldnt happen for a regular code

    assert(getImmPredValue(*SecLastInstr) && "Bad branch predicate (2)!");

    // all following instructions(branches) are obviously dead,
    // since we know that the preceeding branch will be executed
    if (AllowModify) LastInstr->eraseFromParent();

    FBB = 0; TBB = TBB = SecLastInstr->getOperand(0).getMBB();

    // if the destination is the same as the fall-through block, we can
    // drop the second last branch (now actually being the last) as well,
    // since we are now falling through to the layout successor
    if (MBB.isLayoutSuccessor(TBB) && AllowModify) {
      SecLastInstr->eraseFromParent();
      TBB = 0;
    }
    return false;
  }
  else {

    assert(LastInstr && "Can not analyze conditional branch!");

    // set the outcomes of the conditional branch. We do not overcomplicate
    // things such as checking whether we are jumping to the layout follower,
    // let the branch-folder do the rest for us if necessary
    TBB = SecLastInstr->getOperand(0).getMBB();
    FBB = LastInstr->getOperand(0).getMBB();

    // Store the conditions of the conditional jump
    Cond.push_back(SecLastInstr->getOperand(1));  // Zero/NZ
    Cond.push_back(SecLastInstr->getOperand(2));  // Reg
    return false;
  }
 
  // can't analyze 
  return true;
}

//-----------------------------------------------------------------------------

unsigned
TMS320C64XInstrInfo::InsertBranch(MachineBasicBlock &MBB,
		                  MachineBasicBlock *TBB,
                                  MachineBasicBlock *FBB,
		                  const SmallVectorImpl<MachineOperand> &Cond,
                                  DebugLoc DL) const
{
  assert(TBB && "InsertBranch can't insert fallthroughs");
  assert((Cond.size() == 2 || Cond.size() == 0)
    && "Invalid condition to InsertBranch");

  unsigned numInsertedBranches = 0;

  if (Cond.empty()) {

    // We are dealing with an unconditional branch. The branch-analyzer must
    // not supply the false-cond outcome, since there is no condition at all
    assert(!FBB && "Unconditional branch with multiple successors");
    addDefaultPred(BuildMI(&MBB, DL, get(TMS320C64X::branch)).addMBB(TBB));
    numInsertedBranches = 1;
  }
  else {
    // Insert conditional branch with operands sent to us by analyze branch,
    // deal here with the true-cond successor
    BuildMI(&MBB, DL, get(TMS320C64X::branch_cond)).addMBB(TBB)
      .addImm(Cond[0].getImm()).addReg(Cond[1].getReg());

    numInsertedBranches = 1;

    if (FBB) {
      // when dealing with conditional branches without fallthroughs (branch
      // analyzer result), also do a branch insertion to the false-successor
      addDefaultPred(BuildMI(&MBB,
        DL, get(TMS320C64X::branch_cond)).addMBB(FBB));

      numInsertedBranches++;
    }
  }

  return numInsertedBranches;
}

//-----------------------------------------------------------------------------

unsigned TMS320C64XInstrInfo::RemoveBranch(MachineBasicBlock &MBB) const {

  MachineBasicBlock::iterator I = MBB.end();

  unsigned count = 0;

  while (I != MBB.begin()) {
    --I;

    const int opcode = I->getOpcode();

    // skip/ignore any nops (trailing or in between)
    if (opcode == TMS320C64X::noop) continue;

    // We can only consider branch-instruction with known destination, this
    // excludes any non-branch instructions as well a indirect reg branches
    if (!I->getDesc().isBranch() || opcode == TMS320C64X::branch_reg) break;

    I->eraseFromParent();
    I = MBB.end();
    ++count;
  }

  return count;
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::ReverseBranchCondition(
				SmallVectorImpl<MachineOperand> &Cond) const
{
  const int val = Cond[0].getImm() ? 0 : 1;
  Cond[0] = MachineOperand::CreateImm(val);
  return false;
}

//-----------------------------------------------------------------------------

void TMS320C64XInstrInfo::insertNoop(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI) const
{
  int delay = 1; // emit a single-cycle nop
  DebugLoc dl;
//  DebugLoc dl = DebugLoc::getUnknownLoc();
  addDefaultPred(BuildMI(MBB, MI, dl, get(TMS320C64X::noop)).addImm(delay));
}

//-----------------------------------------------------------------------------

void TMS320C64XInstrInfo::insertBundleEnd(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI) const
{
  DebugLoc dl;
//  DebugLoc dl = DebugLoc::getUnknownLoc();
  BuildMI(MBB, MI, dl, get(TMS320C64X::BUNDLE_END));
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::isMoveInstr(const MachineInstr &MI,
                                 unsigned &src_reg,
				 unsigned &dst_reg,
                                 unsigned &src_sub_idx,
				 unsigned &dst_sub_idx) const
{
  if (MI.getDesc().getOpcode() == TMS320C64X::mv) {
    src_sub_idx = 0;
    dst_sub_idx = 0;
    src_reg = MI.getOperand(1).getReg();
    dst_reg = MI.getOperand(0).getReg();

    // If either of the source/destination registers is on the B
    // side, then this isn't just a simple move instruction, it
    // actually has some functionality, ie 1) actually moving the
    // register from an inaccessable side to the accessable one, and
    // 2) preventing me from stabing my eyes out
    //
    // Unfortunately it's even more complex than that: we want to
    // coalesce moves of B15 so that we can make SP-relative loads
    // and stores as normal; but we don't want to do that for any
    // of the B-side argument registers, lest we end up with
    // operations that write directly to the B side, which then
    // touches the only-want-to-use-side-A situation.

    if ((TMS320C64X::BRegsRegClass.contains(src_reg)
    && src_reg != TMS320C64X::B15)
    || (TMS320C64X::BRegsRegClass.contains(dst_reg)
    && dst_reg != TMS320C64X::B15))
      return false;

    return true;
  }

  return false;
}

//-----------------------------------------------------------------------------

unsigned
TMS320C64XInstrInfo::isLoadFromStackSlot(const MachineInstr *MI,
						int &FrameIndex) const
{
  const TargetInstrDesc desc = MI->getDesc();

  if (desc.TSFlags & TMS320C64XII::is_memaccess &&
      !(desc.TSFlags & TMS320C64XII::is_store))
  {
    if (MI->getOperand(0).getReg() == TMS320C64X::B15) {

      MachineOperand op = MI->getOperand(1);

      if (op.isFI()) {
        FrameIndex = op.getIndex();
        return true;
      }
      else {
        // XXX - are there any circumstances where
        // we'll be handed an insn after frame index
        // elimination? I find this unlikely, but just
        // in case
        llvm_unreachable("Found load-from-stack sans "
                         "frame index operand");
      }
    }
  }

  return false;
}

//-----------------------------------------------------------------------------

unsigned TMS320C64XInstrInfo::isStoreToStackSlot(const MachineInstr *MI,
						 int &FrameIndex) const
{
  const TargetInstrDesc desc = MI->getDesc();

  if (desc.TSFlags & TMS320C64XII::is_memaccess
  && desc.TSFlags & TMS320C64XII::is_store)
  {
    if (MI->getOperand(0).getReg() == TMS320C64X::B15) {

      MachineOperand op = MI->getOperand(1);

      if (op.isFI()) {
        FrameIndex = op.getIndex();
        return true;
      }
      else {
        llvm_unreachable("Found store-to-stack sans "
                         "frame index operand");
      }
    }
  }

  return false;
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::isPredicated(const MachineInstr *MI) const {

  int pred_idx, pred_val;

  pred_idx = MI->findFirstPredOperandIdx();

  if (pred_idx == -1) return false;

  pred_val = MI->getOperand(pred_idx).getImm();

  if (pred_val == -1) return false;

  return true;
}

//-----------------------------------------------------------------------------

int TMS320C64XInstrInfo::getOpcodeForSide(int Opcode, int Side) const {

  DenseMap<unsigned*, std::pair<unsigned,unsigned> >::const_iterator I =
    Pseudo2ClusteredTable.find((unsigned*)Opcode);
  if (I == Pseudo2ClusteredTable.end())
    return -1;
  if (Side == 0) // Side A
    return I->second.first;
  else // Side B
    return I->second.second;
}

//-----------------------------------------------------------------------------

const MachineInstrBuilder &
TMS320C64XInstrInfo::addDefaultPred(const MachineInstrBuilder &MIB) {
  return MIB.addImm(-1).addReg(TMS320C64X::NoRegister);
}

//-----------------------------------------------------------------------------

const MachineInstrBuilder &
TMS320C64XInstrInfo::addFormOp(const MachineInstrBuilder &MIB,
                               unsigned fu,
                               bool use_xpath)
{
  unsigned form = fu << 1;
  if (use_xpath) form |= 0x1;
  return MIB.addImm(form);
}

//-----------------------------------------------------------------------------

int TMS320C64XInstrInfo::encodeInOperand(int unit, bool xpath) {
  using namespace TMS320C64XII;

  int code = FU_undef;
  switch (unit) {
    case unit_d: code = FU_D; break;
    case unit_s: code = FU_S; break;
    case unit_l: code = FU_L; break;
    case unit_m: code = FU_M; break;
  }

  if (xpath) ++code;

  assert(code >= FU_L && code < FU_undef);
  return code;
}

//-----------------------------------------------------------------------------

const TargetRegisterClass *
TMS320C64XInstrInfo::findRegisterSide(unsigned reg, const MachineFunction *MF) {
  int j;
  TargetRegisterClass *c;

  TargetRegisterClass::iterator i =
    TMS320C64X::ARegsRegisterClass->allocation_order_begin(*MF);

  c = TMS320C64X::BRegsRegisterClass;

  // Hackity: don't use allocation_order_end, because it won't
  // match instructions that use reserved registers, and they'll
  // incorrectly get marked as being on the other data path side.
  // So instead, we know that there's 32 of them in the A reg
  // class, just loop through all of them

  for (j = 0; j < 32; j++) {
    if ((*i) == reg) {
      c = TMS320C64X::ARegsRegisterClass;
      break;
    }
    i++;
  }

  return c;
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::check_sconst_fits(long int num, int bits) {

  long int maxval = 1 << (bits - 1);

  if (num < maxval && num >= (-maxval)) return true;
  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::check_uconst_fits(unsigned long int num, int bits) {

  unsigned long maxval = 1 << bits;

  if (num < maxval) return true;
  return false;
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::Predicate_sconst_n(SDNode *in, int bits) {
  ConstantSDNode *N = cast<ConstantSDNode>(in);

  int val = N->getSExtValue();
  return check_sconst_fits(val, bits);
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::Predicate_uconst_n(SDNode *in, int bits) {
  ConstantSDNode *N = cast<ConstantSDNode>(in);

  unsigned int val = N->getSExtValue();
  val = abs(val);

  return check_uconst_fits(val, bits);
}

//-----------------------------------------------------------------------------

bool TMS320C64XInstrInfo::Predicate_const_is_positive(SDNode *in) {
  ConstantSDNode *N = cast<ConstantSDNode>(in);

  long val = N->getSExtValue();
  return (val >= 0);
}

//-----------------------------------------------------------------------------

const
TMS320C64XInstrInfo::UnitStrings_t &TMS320C64XInstrInfo::getUnitStrings() {
  return UnitStrings;
}

//-----------------------------------------------------------------------------

void
TMS320C64XInstrInfo::dumpFlags(const MachineInstr *MI, raw_ostream &os) {
  dumpFlags(MI->getDesc(), os);
}

//-----------------------------------------------------------------------------

void
TMS320C64XInstrInfo::dumpFlags(const TargetInstrDesc &Desc, raw_ostream &os) {
  using namespace TMS320C64XII;

#define PRETTY(x) \
  ((x) ? "True" : "False")

  uint64_t flags = Desc.TSFlags;
  os << "Flag value = 0x" << format("%x", flags) << "\n";
  unsigned sup = flags & unit_support_mask;
  if (sup) {
    os << "Flag[Supported on .D] = " << PRETTY(sup & 0x1) << "\n";
    os << "Flag[Supported on .M] = " << PRETTY(sup >>= 1 & 0x1) << "\n";
    os << "Flag[Supported on .S] = " << PRETTY(sup >>= 1 & 0x1) << "\n";
    os << "Flag[Supported on .L] = " << PRETTY(sup >>= 1 & 0x1) << "\n";
  }
  os << "Flags[DefaultUnit] = ." <<UnitStrings[GET_UNIT(flags)] << "\n";

  os << "Flag[Side] = " << (IS_BSIDE(flags) ? "B" : "A") << "\n";
  os << "Flag[DelaySlots] = " << GET_DELAY_SLOTS(flags) << "\n";
  os << "Flag[MemAccess] = " << PRETTY(flags & is_memaccess) << "\n";
  os << "Flag[MemShift] = "
    << ((flags & mem_align_amt_mask) >> mem_align_amt_shift) << "\n";
  os << "Flag[MemLoadStore] = " << ((flags & is_store) ? "Store" : "Load")
    << "\n";
}
