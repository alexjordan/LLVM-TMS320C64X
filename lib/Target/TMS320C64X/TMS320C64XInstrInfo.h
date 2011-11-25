//==- TMS320C64XInstrInfo.h - TMS320C64X Instruction Information -*- C++ -*-==//
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

#ifndef LLVM_TARGET_TMS320C64X_INSTRINFO_H
#define LLVM_TARGET_TMS320C64X_INSTRINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "TMS320C64XHazardRecognizer.h"
#include "TMS320C64XRegisterInfo.h"

namespace llvm {

namespace TMS320C64XII {

enum {
  unit_support_mask = 0xf,
  unit_l = 0,
  unit_s = 1,
  unit_m = 2,
  unit_d = 3,
  is_bside = 0x10,
  side_shift = 4,
  is_memaccess = 0x100,
  is_store = 0x800,
  mem_align_amt_mask = 0x600,
  mem_align_amt_shift = 9,
  fixed_unit_mask = 0x3000,
  fixed_unit_shift = 12,
  is_side_inst = 0x4000
};

// unit support value that signifies: instruction is fixed
#define UNIT_FIXED 0

// helper macros for accessing TSFlags fields
// get default/fixed unit value (2-bits)
#define GET_UNIT(x) (((x) & TMS320C64XII::fixed_unit_mask) \
  >> TMS320C64XII::fixed_unit_shift)
// get the cluster side of an instructions, works for all instructions
#define IS_BSIDE(x) ((x) & TMS320C64XII::is_bside)
// get number of delay slots, also for all instructions
#define GET_DELAY_SLOTS(x) (((x) >> 5) & 0x7)

// bit 0 xflag
// bits 1..2 unit number
enum FUEncoding {
  FU_L = 0, // unit_l
  FU_Lx,
  FU_S,     // unit_s
  FU_Sx,
  FU_M,     // unit_m
  FU_Mx,
  FU_D,     // unit_d
  FU_Dx,
  FU_undef
};

enum Resources {
  NUM_FUS = 4,
  NUM_SIDES = 2
};

enum Sides {
  ASide = 0,
  BSide = 1
};
} // namespace TMS320C64XII

class TMS320C64XInstrInfo : public TargetInstrInfoImpl {
public:
  typedef SmallVector<const char *, 4> UnitStrings_t;

private:
  // store the register info
  const TMS320C64XRegisterInfo RI;

  // maps pseudo instructions to side-specific ones
  DenseMap<unsigned*, std::pair<unsigned,unsigned> > Pseudo2ClusteredTable;
  DenseMap<unsigned, unsigned> Side2SideMap;

  // string representation for functional units and resources
  static UnitStrings_t UnitStrings;
  static std::string Res_a[TMS320C64XII::NUM_FUS];
  static std::string Res_b[TMS320C64XII::NUM_FUS];

public:

//    explicit TMS320C64XInstrInfo(TMS320C64XTargetMachine &TM);
    // does not have to be explicit any longer since no args used
    TMS320C64XInstrInfo();

    virtual const TMS320C64XRegisterInfo &getRegisterInfo() const {
      return RI;
    }

    // creates a generic hazard recognizer
    ScheduleHazardRecognizer*
    CreateTargetHazardRecognizer(const TargetMachine *TM,
                                 const ScheduleDAG *DAG) const;

    // AJO hook used by the custom post RA scheduler
    static ScheduleHazardRecognizer*
    CreatePostRAHazardRecognizer(const TargetMachine *TM);

    // creates the target specific FU scheduler
    static TMS320C64X::ResourceAssignment*
    CreateFunctionalUnitScheduler(const TargetMachine *TM);

    virtual void copyPhysReg(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I,
                             DebugLoc DL,
                             unsigned DestReg,
                             unsigned SrcReg,
                             bool KillSrc) const;

    virtual bool copyRegToReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I,
                              unsigned destReg,
                              unsigned srcReg,
                              const TargetRegisterClass *dstRC,
                              const TargetRegisterClass *srcRC) const;

    // NKIM, signature changed for llvm versions higher than 2.7
    virtual void storeRegToStackSlot(MachineBasicBlock &MBB,
				     MachineBasicBlock::iterator I,
				     unsigned srcReg,
                                     bool isKill,
                                     int frameIndex,
                                     const TargetRegisterClass*,
                                     const TargetRegisterInfo*) const;

    // NKIM, signature changed for llvm versions higher than 2.7
    virtual void loadRegFromStackSlot(MachineBasicBlock &MBB,
				      MachineBasicBlock::iterator MI,
				      unsigned dstReg,
                                      int frameIndex,
				      const TargetRegisterClass*,
                                      const TargetRegisterInfo*) const;

    virtual bool AnalyzeBranch(MachineBasicBlock &MBB,
			       MachineBasicBlock *&TBB,
			       MachineBasicBlock *&FBB,
			       SmallVectorImpl<MachineOperand> &Cond,
			       bool AllowModify = false) const;

    // NKIM, DebugLocation has been added to the parameter list
    virtual unsigned InsertBranch(MachineBasicBlock &,
				  MachineBasicBlock *,
				  MachineBasicBlock *,
				  const SmallVectorImpl<MachineOperand> &Cond,
                                  DebugLoc) const;

    virtual unsigned RemoveBranch(MachineBasicBlock &MBB) const;

    virtual bool ReverseBranchCondition(SmallVectorImpl<MachineOperand>
					&Cond) const;

    virtual void insertNoop(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI) const;

    // NKim, provided for the TI backend, stub in TargetInstrInfo
    virtual void insertBundleEnd(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI) const;

    virtual bool isMoveInstr(const MachineInstr& MI,
			     unsigned& SrcReg,
                             unsigned& DstReg,
			     unsigned& SrcSubIdx,
                             unsigned& DstSubIdx) const;

    virtual unsigned isLoadFromStackSlot(const MachineInstr *MI,
					 int &FrameIndex) const;

    virtual unsigned isStoreToStackSlot(const MachineInstr *MI,
					int &FrameIndex) const;

    virtual bool isPredicated(const MachineInstr *MI) const;

    // returns cluster side for MI (ASide = 0, BSide = 1)
    static int getSide(const MachineInstr *MI);

    // defunct. was used with pseudo opcodes.
    int getOpcodeForSide(int Opcode, int Side) const;

    // used to map an instruction to its version on the opposite cluster side.
    int getSideOpcode(int Opcode, int Side) const;

    // returns true if the instruction has flexible encoding.
    // (ie. it can be scheduled on different FUs via an operand and there exists
    // an opcode for the opposite cluster side.
    static bool isFlexible(const TargetInstrDesc &tid);
    static bool isFlexible(const MachineInstr *MI);

    // NKIM, NON-LLVM-custom stuff, put inside the class, made static, removed
    // implementation bodies in order to avoid header-confusions and building
    // problems

    bool getImmPredValue(const MachineInstr &MI) const;

    static
    const MachineInstrBuilder &addDefaultPred(const MachineInstrBuilder &MIB);

    static
    const MachineInstrBuilder &addFormOp(const MachineInstrBuilder &MIB,
                                         unsigned fu,
                                         bool use_xpath = false);

    static
    const TargetRegisterClass *findRegisterSide(unsigned reg,
                                                const MachineFunction *MF);

    static int encodeInOperand(int unit, bool xpath);

    static bool check_sconst_fits(long int num, int bits);
    static bool check_uconst_fits(unsigned long int num, int bits);
    static bool Predicate_sconst_n(SDNode *in, int bits);
    static bool Predicate_uconst_n(SDNode *in, int bits);
    static bool Predicate_const_is_positive(SDNode *in);

    // helpers for string conversion
    static const UnitStrings_t &getUnitStrings();
    static std::string res2Str(int r) {
      using namespace TMS320C64XII;
      assert(r >= 0 && r < NUM_FUS * NUM_SIDES);
      std::string (&c)[4] = ((r & 0x1) == ASide) ? Res_a : Res_b;
      return c[r >> 1];
    }

    static void dumpFlags(const MachineInstr *MI, raw_ostream &os);
    static void dumpFlags(const TargetInstrDesc &Desc, raw_ostream &os);
};

} // llvm

#endif // LLVM_TARGET_TMS320C64X_INSTRINFO_H
