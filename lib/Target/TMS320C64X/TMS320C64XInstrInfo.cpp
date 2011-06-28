//===- TMS320C64XInstrInfo.cpp - TMS320C64X Instruction Information -------===//
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
static const char *unit_str[] = { "L", "S", "M", "D" };
TMS320C64XInstrInfo::UnitStrings_t
  TMS320C64XInstrInfo::UnitStrings(unit_str, unit_str + 4);

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

  // Only use hazard recognizer with post-RA scheduling setup
  if (TM->getSubtarget<TMS320C64XSubtarget>().enablePostRAScheduler()) {
    assert(false && "not yet implemented/tested");
    return new TMS320C64XHazardRecognizer(*TII);
  }

  // Otherwise use LLVM default
  return TargetInstrInfoImpl::CreateTargetHazardRecognizer(TM, DAG);
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
                                         const TargetRegisterClass*,
                                         const TargetRegisterInfo*) const
{
  DebugLoc DL;
  if  (MI != MBB.end()) DL = MI->getDebugLoc();

  addFormOp(
    addDefaultPred(BuildMI(MBB, MI, DL, get(TMS320C64X::word_store_1))
      .addReg(TMS320C64X::A15).addFrameIndex(frameIndex)
      .addReg(srcReg, getKillRegState(isKill))),
    TMS320C64XII::unit_d);
}

//-----------------------------------------------------------------------------

void
TMS320C64XInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
		                          MachineBasicBlock::iterator MI,
                                          unsigned dstReg,
                                          int frameIndex,
		                          const TargetRegisterClass*,
                                          const TargetRegisterInfo*) const
{
  DebugLoc DL;
  if  (MI != MBB.end()) DL = MI->getDebugLoc();

  addFormOp(
    addDefaultPred(BuildMI(MBB, MI, DL, get(TMS320C64X::word_load_1))
      .addReg(dstReg, RegState::Define)
      .addReg(TMS320C64X::A15).addFrameIndex(frameIndex)),
    TMS320C64XII::unit_d);
}

//-----------------------------------------------------------------------------

bool
TMS320C64XInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
		                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const
{
	bool predicated, saw_uncond_branch;
	int pred_idx, opcode;
	MachineBasicBlock::iterator I = MBB.end();

	saw_uncond_branch = false;

	while (I != MBB.begin()) {
		--I;

		pred_idx = I->findFirstPredOperandIdx();
		if (pred_idx == -1) {
			predicated = false;
		} else if (I->getOperand(pred_idx).getImm() != -1) {
			predicated = true;
		} else {
			predicated = false;
		}

		opcode = I->getOpcode();

		if (!predicated && (opcode == TMS320C64X::branch_p ||
					opcode == TMS320C64X::branch_1 ||
					opcode == TMS320C64X::branch_2)) {
			// We're an unconditional branch. The analysis rules
			// say that we should carry on looking, in case there's
			// a conditional branch beforehand.

			saw_uncond_branch = true;
			TBB = I->getOperand(0).getMBB();

			if (!AllowModify)
				// Nothing to be done
				continue;

			// According to what X86 does, we can delete branches
			// if they branch to the immediately following BB

			if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
				TBB = NULL;
				// FIXME - and what about trailing noops?
				I->eraseFromParent();
				I = MBB.end();
				continue;
			}

			continue;
		}

		// If we're a predicated instruction and terminate the BB,
		// we can be pretty sure we're a conditional branch
		if (predicated && (opcode == TMS320C64X::brcond_p)) {
			// Two different conditions to consider - this is the
			// only branch, in which case we fall through to the
			// next, or it's a conditional before unconditional.

			if (TBB != NULL) {
				// False: the trailing unconditional branch
				// True: the conditional branch if taken
				FBB = TBB;
				TBB = I->getOperand(0).getMBB();
			} else {
				TBB = I->getOperand(0).getMBB();
			}

			// Store the conditions of the conditional jump
			Cond.push_back(I->getOperand(1)); // Zero/NZ
			Cond.push_back(I->getOperand(2)); // Reg

			return false;
		}

		// Out of branches and conditional branches, only other thing
		// we expect to see is a trailing noop

		if (opcode == TMS320C64X::noop)
			continue;

		if (saw_uncond_branch)
			// We already saw an unconditional branch, then
			// something we didn't quite understand
			return false;

		return true; // Something we don't understand at all
	}

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

  if (Cond.empty()) {
    // Unconditional branch
    assert(!FBB && "Unconditional branch with multiple successors");
    addDefaultPred(BuildMI(&MBB, DL, get(TMS320C64X::branch_p)).addMBB(TBB));
  }
  else {
    // Insert conditional branch with operands sent to us by analyze branch
    BuildMI(&MBB, DL, get(TMS320C64X::brcond_p)).addMBB(TBB)
      .addImm(Cond[0].getImm()).addReg(Cond[1].getReg());

    if (FBB)
      addDefaultPred(BuildMI(&MBB, DL, get(TMS320C64X::brcond_p)).addMBB(FBB));
  }
  return 1;
}

//-----------------------------------------------------------------------------

unsigned
TMS320C64XInstrInfo::RemoveBranch(MachineBasicBlock &MBB) const {

  MachineBasicBlock::iterator I = MBB.end();

  unsigned count = 0;


  while (I != MBB.begin()) {
    --I;

    if (I->getOpcode() != TMS320C64X::branch_p &&
        I->getOpcode() != TMS320C64X::branch_1 &&
        I->getOpcode() != TMS320C64X::branch_2 &&
        I->getOpcode() != TMS320C64X::brcond_p &&
        I->getOpcode() != TMS320C64X::noop)
        break;

    // Remove branch
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
