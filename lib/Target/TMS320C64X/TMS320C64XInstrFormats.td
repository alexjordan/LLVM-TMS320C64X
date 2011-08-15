//===- TMS320C64XInstrFormats.td - TMS320C64X Insn formats -*- tablegen -*-===//
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

///////////////////////////
/// OPERAND DEFINITIONS ///
///////////////////////////

// NKim, this operand is useful when dealing with indirect calls specific
// to the TI which does not natively support register calls. Now, as a work-
// around we create a label, push it into B3, issue a call, and then put
// the label right after the call-instruction

def LabelOperand : Operand<iPTR>;

// This input operand is used for the regular (non register) calls, which go
// to a global address or to an external symbol

def CallTargetOperand : Operand<i32>;

def BranchTargetOperand : Operand<OtherVT>;

class UnitSupport<bit l, bit s, bit m, bit d> {
  bits<4> units = { d, m, s, l };
}

def units_fixed : UnitSupport<0,0,0,0>;
def units_any   : UnitSupport<1,1,1,1>;
def units_s     : UnitSupport<0,1,0,0>;
def units_m     : UnitSupport<0,0,1,0>;
def units_d     : UnitSupport<0,0,0,1>;
def units_notm  : UnitSupport<1,1,0,1>;

class Delay<bits<3> val> {
  bits<3> value = val;
}

class InstUnit<bits<2> u> {
  bits<2> unit = u;
}

def unit_l      : InstUnit<0>;
def unit_s      : InstUnit<1>;
def unit_m      : InstUnit<2>;
def unit_d      : InstUnit<3>;

// the form operand used to define flexible parts of an instruction.
// note: it does NOT define the cluster side, separate instructions are
// generated so that appropriate register classes can be used.
class InstructionForm<dag OpTypes, int default>
  : OptionalDefOperand<OtherVT, OpTypes, (ops (i32 default))> {
  let PrintMethod = "printFU";
}

// maps units to the encoding of the instruction form operand used in flexible
// instructions.
class FormEncoding<InstUnit iu> {
  // a 3-bit immediate where the upper 2 bits encode the functional unit and the
  // rightmost bit signals use of the xpath (datapath for loads/stores)
  bits<3> imm = 0;
  let imm{2-1} = iu.unit;
}

// the following can be used to define default units per instruction
def default_form : InstructionForm<(ops timm), -1>;
def l_form : InstructionForm<(ops timm), FormEncoding<unit_l>.imm>;
def s_form : InstructionForm<(ops timm), FormEncoding<unit_s>.imm>;
def m_form : InstructionForm<(ops timm), FormEncoding<unit_m>.imm>;
def d_form : InstructionForm<(ops timm), FormEncoding<unit_d>.imm>;


// here is me not being able to cast int to bit...
class InstSide<int si, bit sb> {
  int intval = si;
  bit bitval = sb;
}

def side_a : InstSide<0,0>;
def side_b : InstSide<1,1>;

def mem_operand	: Operand<i32> {
	let PrintMethod = "printMemOperand";
	let MIOperandInfo = (ops GPRegs, GPRegs);
}

// NKIM, for the time being we require the root-node to be supplied
def addr : ComplexPattern<i32, 2, "select_addr", [frameindex], [SDNPWantRoot]>;

// AJO: let select_addr defer to select_idxaddr internally (no more _idx instrs)
//def idxaddr : ComplexPattern<i32, 2, "select_idxaddr", [frameindex], [SDNPWantRoot]>;

def uconst4_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getSExtValue(), MVT::i32);
}]>;
def uconst4 : PatLeaf<(imm), [{
	int val = (int)N->getSExtValue();
	return (val <= 15 && val >= 0);
}], uconst4_xform>;

def sconst5_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getSExtValue(), MVT::i32);
}]>;
def sconst5 : PatLeaf<(imm), [{
	int val = (int)N->getSExtValue();
	return (val <= 15 && val >= -16);
}], sconst5_xform>;

def uconst5_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getZExtValue(), MVT::i32);
}]>;
def uconst5 : PatLeaf<(imm), [{
	int val = (int)N->getZExtValue();
	return (val >= 0 && val < 32);
}], uconst5_xform>;

def uconst15_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getZExtValue(), MVT::i32);
}]>;
def uconst15 : PatLeaf<(imm), [{
	int val = (int)N->getZExtValue();
	return (val < 0x7FF);
}], uconst15_xform>;

def sconst16_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getZExtValue(), MVT::i32);
}]>;
def sconst16 : PatLeaf<(imm), [{
	int val = (int)N->getZExtValue();
	return (val >= -0x800 && val < 0x7FF);
}], sconst16_xform>;

def mvk_xform : SDNodeXForm<imm, [{
	return CurDAG->getTargetConstant((int)N->getSExtValue(), MVT::i32);
}]>;

// return true if the immediate falls into the sign-extended 16 bit range (i.e.
// hex 7FFF and -8000). Does semantically the same thing as the mvk_pred leaf,
// but has a much better readability

def isSigned16imm : PatLeaf<(imm), [{
  int val = (int)N->getSExtValue();
  return (val < 0x7FFF && val >= -0x8000);
}], mvk_xform>;

def mvk_pred : PatLeaf<(imm), [{
	int val = (int)N->getSExtValue();
	return (val < 0x7FFF && val >= -0x8000);
}], mvk_xform>;

def mvk_all_pred : PatLeaf<(imm), [{
	return (N == N); // Avoid unused N warning
}], NOOP_SDNodeXForm>;

// ~

def set_lo : PatLeaf<(imm), [{
	unsigned int val = N->getZExtValue();
	return ((val & 0xFFFF) == val);
}]>;

def set_hi : PatLeaf<(imm), [{
	unsigned int val = (unsigned)N->getZExtValue();
	return ((val & 0xFFFF0000) == val);
}]>;

def pred : PredicateOperand<OtherVT, (ops timm, PredRegs),
			(ops (i32 -1), (i32 zero_reg))> {
	// Predicate of -1 indicates that this is always-execute
}

def nodefault_pred : PredicateOperand<OtherVT, (ops timm, PredRegs),
			(ops)>;

def nodefault_zflag : PredicateOperand<i32, (ops timm), (ops)>;
def nodefault_reg : PredicateOperand<i32, (ops PredRegs), (ops)>;

// NKIM, supply the root-node to the func, not sure about parent or root
def predwrapper1 : ComplexPattern<i32, 1, "bounce_predicate", [], [SDNPWantRoot]>;

class inst<dag outops,
           dag inops,
           string asm_str,
           list<dag> pattern,
           bit side,
           InstUnit u>
: Instruction {

  // All insns are conditional unless stated otherwise
  let isPredicable = 1;
  let CodeSize = 4;

  let Namespace = "TMS320C64X";
  let OutOperandList = outops;

// NKIM, that causes tblgen crash for llvm higher than 2.7
//  let InOperandList = !con(ins, (ops pred:$s));
  let InOperandList = !con(inops, (ins pred:$s));
  let Itinerary = Default;

  let neverHasSideEffects = 1;
  let AsmString = asm_str;

  // Match nothing; set sched_pattern for a future tablegen backend
  // to play with. pseudo instrs over-ride this behavior.
  list<dag> sched_pattern = pattern;
  let Pattern = [];

  UnitSupport Supported = units_fixed;
  bits<3> DelaySlots = 0;
  bit MemAccess = 0;
  bits<2> MemShift = 0;
  bit MemLoadStore = 0;
  // default unit for 'mobile' (not units_fixed) instructions, otherwise
  // the single legal unit.
  InstUnit DefaultUnit = u;
  bit SideInst = 0; // no, this is a fixed/pseudo-fixed inst

  let TSFlags{3-0} = Supported.units; // unit support
  let TSFlags{4} = side; // cluster side (0: A, 1: B)
  let TSFlags{7-5} = DelaySlots; // (range: [0,5])
  let TSFlags{8} = MemAccess; // (instr is load/store)
  let TSFlags{10-9} = MemShift; // mem_shift_amt (XXX)
  let TSFlags{11} = MemLoadStore; // (0 for load, 1 for store)
  let TSFlags{13-12} = DefaultUnit.unit; // fixed/default unit
  let TSFlags{14} = SideInst; // inst or c64sideinst
}

class pseudoinst<dag outops, dag inops, string asm, list<dag> pat>
  : inst<outops, inops, asm, pat, 0, unit_d>
{
  let InOperandList = inops;
  let Pattern = pat;

  let Itinerary = NoItinerary;

  // emit them (as asm comments) for the time being
  //let isCodeGenOnly = 1;
}

class nopred_inst<dag outops,
                  dag inops,
                  string mnemonic,
                  list<dag> pattern,
		  bit side,
                  InstUnit u> 
: inst<outops, inops, mnemonic, pattern, side, u>
{
//  let InOperandList = !con(ins, (ops nodefault_zflag:$a, nodefault_reg:$b));
  let InOperandList = !con(inops, (ins nodefault_zflag:$a, nodefault_reg:$b));
}

class memload<dag outs,
              dag ins,
              list<dag> pattern,
              bit side,
              InstUnit u>
: inst<outs, ins, "", pattern, side, u>
{
  let DelaySlots = 4;
  let hasDelaySlot = 1;

  let MemAccess = 1;
  let MemLoadStore = 0;

  let Itinerary = Load;
  let Supported = units_d;

}

class memstore<dag outs,
               dag ins,
               list<dag> pattern,
               bit side,
               InstUnit u>
: inst<outs, ins, "", pattern, side, u> 
{
  let MemAccess = 1;
  let MemLoadStore = 1;
  let Supported = units_d;
}

class pseudo_rr<string mnemonic,
                string sidespec,
                InstUnit u, 
                SDNode op,
                int swap>
: inst<(outs ARegs:$dst), (ins ARegs:$src1, ARegs:$src2), "",
       [(set ARegs:$dst, (op ARegs:$src1, ARegs:$src2))], 0, u>
{
  string comb_mnemonic = !strconcat(!strconcat(mnemonic, "\t"), sidespec);
  let AsmString = !if(swap,

  !strconcat(comb_mnemonic,"\t$src2,\t$src1,\t$dst"),
  !strconcat(comb_mnemonic, "\t$src1,\t$src2,\t$dst"));

  let Pattern = sched_pattern;
}

class pseudo_ri<string mnemonic, string sidespec, dag imm, InstUnit u,
                SDNode op, int swap>
: inst<(outs ARegs:$dst), (ins ARegs:$src1, i32imm:$imm), "",
        [(set ARegs:$dst, (op ARegs:$src1, imm))], 0, u>
{
  string comb_mnemonic = !strconcat(!strconcat(mnemonic, "\t"), sidespec);
  let AsmString = !if(swap,
  !strconcat(comb_mnemonic, "\t$imm,\t$src1,\t$dst"),
  !strconcat(comb_mnemonic, "\t$src1,\t$imm,\t$dst"));
  let Pattern = sched_pattern;
}

////////////////////////////////////////////////////////////////////////////
// this class has been provided to capture the mvkl/mvkh instruction variants.
// These instructions are supposed to be executed on the S-unit only. Since
// there is no real instruction for loading a 32-bit immediate into a register,
// this is done by decomposing it into a "move high" immediately following a
// "move low" instruction. For that reason we do not specify a pattern, but
// match the instruction explicitly later. NOTE, we do not restrict the inst-
// ruction by the register-side constraints

class moveInstr<dag outs, dag ins, string asm, bit side, InstUnit u>
: inst<outs, ins, asm, [], side, u>;

////////////////////////////////////////////////////////////////////////////
// following multiclass is for capturing the mvkl-instruction variants which
// are used in pairs for loading an immediate value (or a label address) in-
// to a register. We allow the unit-string to be passed in order to be able
// to leave the unit empty, i.e. to leave the assignment up to the assembler

multiclass moveLowInstr<string asmString, string unitString, bit side> {
let isMoveImm = 1 in {
  def _immediate : moveInstr<(outs GPRegs:$dst),
                             (ins i32imm:$imm, pred:$prd),
                             !strconcat(asmString, !strconcat("\t",
                             !strconcat(unitString, "\t$imm,\t$dst"))),
                             side, unit_s>;

  // both, moves to register-side A and B must be done in consistence with
  // with the unit-selection. A move to one of the A-registers has to be
  // done on the unit 1 (L1, S1, D1), the same applies to the B-registers.
  // TODO, for the time being we utilize the S-units only

  def _immediate_A : moveInstr<(outs ARegs:$dst),
                               (ins i32imm:$imm, ARegs:$src1),
                               !strconcat(asmString, "\t.S1\t$imm,\t$dst"),
                               0, unit_s>;

  def _immediate_B : moveInstr<(outs BRegs:$dst),
                               (ins i32imm:$imm, BRegs:$src1),
                               !strconcat(asmString, "\t.S2\t$imm,\t$dst"),
                               1, unit_s>;

  // NOTE, moving a label into a register can only be done on the S2 unit

  def _label : moveInstr<(outs GPRegs:$dst),
                         (ins LabelOperand:$label, pred:$prd),
                         !strconcat(asmString, "\t.S2\t$label,\t$dst"),
                         1, unit_s>;
}
}

////////////////////////////////////////////////////////////////////////////
// following multiclass is for capturing the mvkh-instruction variants which
// are used in pairs for loaing an immediate values (or a label address) in-
// to a register. Hereby we assume the mvkh instruction always to exist in
// pairs with the mvkl instruction, therefore the instruction is restricted
// to use the same register as a source/destination

multiclass moveHighInstr<string asmString, string unitString, bit side> {
let Constraints = "$src1 = $dst", isMoveImm = 1 in {

  def _immediate : moveInstr<(outs GPRegs:$dst),
                             (ins i32imm:$imm, GPRegs:$src1),
                             !strconcat(asmString, !strconcat("\t",
                             !strconcat(unitString, "\t$imm,\t$dst"))),
                             side, unit_s>;

  def _immediate_A : moveInstr<(outs ARegs:$dst),
                               (ins i32imm:$imm, ARegs:$src1),
                               !strconcat(asmString, "\t.S1\t$imm,\t$dst"),
                               0, unit_s>;

  def _immediate_B : moveInstr<(outs BRegs:$dst),
                               (ins i32imm:$imm, BRegs:$src1),
                               !strconcat(asmString, "\t.S2\t$imm,\t$dst"),
                               1, unit_s>;

  def _label : moveInstr<(outs GPRegs:$dst),
                         (ins LabelOperand:$label, GPRegs:$src1),
                         !strconcat(asmString, "\t.S2\t$label,\t$dst"),
                         1, unit_s>;
}
}

multiclass meminst_load_p<string mnemonic, PatFrag op> {

  def _load_p_addr : memload<(outs GPRegs:$dst), (ins mem_operand:$ptr),
                     [(set GPRegs:$dst, (op addr:$ptr))], 0, unit_d>
  {
    let Pattern = sched_pattern;
    let AsmString = !strconcat("ld",!strconcat(mnemonic, "\t\t$ptr,\t$dst"));
  }

  def _load_p_idx : memload<(outs GPRegs:$dst), (ins mem_operand:$ptr),
                    [(set GPRegs:$dst, (op addr:$ptr))], 0, unit_d>
  {
    let Pattern = sched_pattern;
    let AsmString = !strconcat("ld",!strconcat(mnemonic, "\t\t$ptr,\t$dst"));
  }
}

multiclass meminst_store_p<string mnemonic, PatFrag op> {

  def _store_p_addr : memstore<(outs), (ins mem_operand:$ptr,GPRegs:$reg),
                      [(op GPRegs:$reg, addr:$ptr)], 0, unit_d>
  {
    let Pattern = sched_pattern;
    let AsmString = !strconcat("st",!strconcat(mnemonic, "\t\t$reg,\t$ptr"));
  }

  def _store_p_idx : memstore<(outs), (ins mem_operand:$ptr, GPRegs:$reg),
                     [(op GPRegs:$reg, addr:$ptr)], 0, unit_d>
  {
    let Pattern = sched_pattern;
    let AsmString = !strconcat("st",!strconcat(mnemonic, "\t\t$reg,\t$ptr"));
  }
}

multiclass meminst_p<string mnemonic, PatFrag loadop, PatFrag storeop> :
  meminst_load_p<mnemonic, loadop>,
  meminst_store_p<mnemonic, storeop>;

///////////////////////////////////////////////////////////////////////////////
// side-specific instructions                                                //
///////////////////////////////////////////////////////////////////////////////

class c64sideinst<dag outops, dag inops, string mnemonic, InstSide side>
: Instruction
{
  // All insns are conditional unless stated otherwise
  let isPredicable = 1;
  let CodeSize = 4;

  let Namespace = "TMS320C64X";

  let OutOperandList = outops;
  let InOperandList = !con(inops, (ins pred:$s, default_form:$fu));

  let Itinerary = Default;
  let neverHasSideEffects = 1;

  //string sidestr = !strconcat("\t.", !if(side.intval, "1", "0"));
  let AsmString = !strconcat(mnemonic, "\t.$fu\t$src1,\t$src2,\t$dst");

  // side-specific instructions don't match
  let Pattern = [];

  UnitSupport Supported = units_any; // different to inst class
  bits<3> DelaySlots = 0;
  bit MemAccess = 0;
  bits<2> MemShift = 0;
  bit MemLoadStore = 0;
  InstUnit DefaultUnit = InstUnit<0>; // not used for this inst kind
  bit SideInst = 1; // yes this is a flexible c64sideinst

  let TSFlags{3-0} = Supported.units; // unit support
  let TSFlags{4} = side.bitval; // cluster side (0: A, 1: B)
  let TSFlags{7-5} = DelaySlots; // (range: [0,5])
  let TSFlags{8} = MemAccess; // (instr is load/store)
  let TSFlags{10-9} = MemShift; // mem_shift_amt (XXX)
  let TSFlags{11} = MemLoadStore; // (0 for load, 1 for store)
  let TSFlags{13-12} = DefaultUnit.unit; // fixed/default unit
  let TSFlags{14} = SideInst; // inst or c64sideinst
}

class c64new<dag outops, dag inops, string mnemonic, InstructionForm format,
             InstSide side>
    : c64sideinst<outops, inops, mnemonic, side> {

  let InOperandList = !con(inops, (ins pred:$s, format:$fu));
}

multiclass c64inst<dag indag, string mnemonic> {
        def _1 : c64sideinst<(outs ARegs:$dst), !con((ins ARegs:$src1), indag),
    mnemonic, side_a>;
        def _2 : c64sideinst<(outs BRegs:$dst), !con((ins BRegs:$src1), indag),
    mnemonic, side_b>;
}

multiclass c64rr<dag inDag, dag inOp, SDNode op, InstructionForm default,
                 string mnemonic> {
  def _1 : c64new<(outs ARegs:$dst), !con((ins ARegs:$src1), inOp),
                  mnemonic, default, side_a> {
    let Pattern = [(set ARegs:$dst, (op ARegs:$src1, inDag))];
  }
  def _2 : c64new<(outs BRegs:$dst), !con((ins BRegs:$src1), inOp),
                  mnemonic, default, side_b>;
}

// immediate version ($src2 is the immediate and and $src1 supports xpath)
multiclass c64ri<dag inDag, dag inOp, SDNode op, InstructionForm default,
                 string mnemonic> {
  def _1 : c64new<(outs ARegs:$dst), !con((ins GPRegs:$src1), inOp),
                       mnemonic, default, side_a> {
    let Pattern = [(set ARegs:$dst, (op GPRegs:$src1, inDag))];
  }
  def _2 : c64new<(outs BRegs:$dst), !con((ins GPRegs:$src1), inOp),
                       mnemonic, default, side_b>;
}

// swapped version (src operands swapped for added confusion)
// used by instructions like shift: (TI's ASM reference) src2, src1, dst.
// LLVM has the src ops swapped and therefore $src2 must be side-fixed.
multiclass c64swp<dag inDag, dag inOp, SDNode op, InstructionForm default,
                  string mnemonic> {
  def _1 : c64new<(outs ARegs:$dst), !con(inOp, (ins ARegs:$src2)),
                  mnemonic, default, side_a> {
    let Pattern = [(set ARegs:$dst, (op inDag, ARegs:$src2))];
  }
  def _2 : c64new<(outs BRegs:$dst), !con(inOp, (ins BRegs:$src2)),
                  mnemonic, default, side_b>;
}

// side-specific load/store

def mem_op_a: Operand<i32> {
        let PrintMethod = "printMemOperand";
        let MIOperandInfo = (ops ARegs, ARegs);
}

def mem_op_b: Operand<i32> {
        let PrintMethod = "printMemOperand";
        let MIOperandInfo = (ops BRegs, BRegs);
}

class c64sideload<dag indag, dag outdag, string load, InstSide side> :
        c64new<outdag, indag, "", d_form, side> {
  let DelaySlots = 4;
  let hasDelaySlot = 1;
  let MemAccess = 1;
  let MemLoadStore = 0;
  let mayLoad = 1;
  let Itinerary = Load;

  // restrict to D unit (note: load/store is not fixed, as we assign sides)
  let Supported = units_d;

  let AsmString = !strconcat("ld",!strconcat(load, "\t.$fu\t$ptr,\t$dst"));
}

class c64sidestore<dag indag, string store, InstSide side> :
        c64new<(outs), !con(indag, (ins GPRegs:$reg)), "", d_form, side> {
  let MemAccess = 1;
  let MemLoadStore = 1;
  let mayStore = 1;

  // restrict to D unit (note: load/store is not fixed, as we assign sides)
  let Supported = units_d;

  let AsmString = !strconcat("st",!strconcat(store,
        "\t.$fu\t$reg,\t$ptr"));
}

// non-restricted loads (not used in ISel)
multiclass c64load<string width> {
  def _load_1 : c64sideload<(ins mem_op_a:$ptr), (outs GPRegs:$dst), width, side_a>;
  def _load_2 : c64sideload<(ins mem_op_b:$ptr), (outs GPRegs:$dst), width, side_b>;
}

multiclass c64strictload<string width, PatFrag op> {
  def _sload_1 : c64sideload<(ins mem_op_a:$ptr), (outs ARegs:$dst), width, side_a> {
    let Pattern = [(set ARegs:$dst, (op addr:$ptr))];
  }
  def _sload_2 : c64sideload<(ins mem_op_b:$ptr), (outs BRegs:$dst), width, side_b>;
}

multiclass c64store<string width, PatFrag op> {
  def _store_1 : c64sidestore<(ins mem_op_a:$ptr), width, side_a> {
    let Pattern = [(op GPRegs:$reg, addr:$ptr)];
  }
  def _store_2 : c64sidestore<(ins mem_op_b:$ptr), width, side_b>;
}
