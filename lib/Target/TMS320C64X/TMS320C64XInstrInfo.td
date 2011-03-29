//==- TMS320C64XInstrInfo.td - TMS320C64X Instruction table -*- tablegen -*-==//
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

////////////////////////
/// Type constraints ///
////////////////////////

// standard type constraint definitions for the call-sequence-start, call-seq-
// end the call node. NOTE, -1 specified for the call type-constraint signals,
// that the node may take a variable number of arguments

def SDT_call_seq_start : SDCallSeqStart<[SDTCisVT<0, i32>]>;
def SDT_call_seq_end : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

// it seems, the -1 signaling a node being variadic doesn't really change much
// in llvm 2.9. With or without the node being explicitly declared as Variadic
// both, 1 and -1 work for us now. I leave it this way for compatibility

def SDT_call : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

// NKim, type constraints for the node corresponding to the loading of a label
// into a TI register (B3), to be able to return back from the subroutine which
// is called indirectly

def SDT_call_label_operand : SDTypeProfile<1, 1,
                                           [SDTCisVT<0, i32>,
                                            SDTCisVT<1, iPTR>]>;

// NKim, type constraints for the node which corresponds to the actual return
// node for the indirectly called subroutine. This is represented as a targets
// external symbol for now and represents a simple label

def SDT_call_return_label : SDTypeProfile<0, 1, [SDTCisVT<0, iPTR>]>;

// type constraints for the node corresponding to a conditional branch, this
// node does not produce anything, takes 3 arguments, a conditional chain, and
// two alternative outcomes

def SDT_brcond : SDTypeProfile<0, 3, [SDTCisVT<0, OtherVT>,
                                     SDTCisVT<1, i32>,
                                     SDTCisVT<2, i32>]>;

// type constraints for the target specific TI implementation of the select-
// instruction

def SDT_tmsselect : SDTypeProfile<1, 4, [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                        SDTCisVT<2, i32>, SDTCisVT<3, i32>,
                                        SDTCisVT<4, i32>]>;

def SDT_Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

////////////////////////
/// Node definitions ///
////////////////////////

def brcond_node : SDNode<"TMSISD::BRCOND", SDT_brcond, [SDNPHasChain]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_call_seq_start,
				[SDNPHasChain, SDNPOutGlue]>;

def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_call_seq_end,
				[SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// NKim, actual node corresponding to the node for moving a label into B3
// register for the subroutine to return to, example "move .tmpLabel, B3"

def call_label_operand_node : SDNode<"TMSISD::RETURN_LABEL_OPERAND",
                                      SDT_call_label_operand,
                                      [SDNPHasChain, SDNPOptInGlue,
                                      SDNPOutGlue, SDNPSideEffect]>;

// NKim, actual node corresponding to the label for returns from indirectly
// called subroutines, this label is to be emitted just after the actual
// branch (b) instruction has been issued

def call_return_label_node : SDNode<"TMSISD::RETURN_LABEL",
                                     SDT_call_return_label,
                                     [SDNPHasChain, SDNPOptInGlue,
                                     SDNPOutGlue, SDNPSideEffect]>;

def cmpeq_node : SDNode<"TMSISD::CMPEQ", SDTIntBinOp,
				[SDNPOutGlue, SDNPCommutative]>;
def cmpne_node : SDNode<"TMSISD::CMPNE", SDTIntBinOp,
				[SDNPOutGlue, SDNPCommutative]>;

def cmpgt_node : SDNode<"TMSISD::CMPGT", SDTIntBinOp, [SDNPOutGlue]>;

def cmpgtu_node : SDNode<"TMSISD::CMPGTU", SDTIntBinOp, [SDNPOutGlue]>;

def cmplt_node : SDNode<"TMSISD::CMPLT", SDTIntBinOp, [SDNPOutGlue]>;

def cmpltu_node : SDNode<"TMSISD::CMPLTU", SDTIntBinOp, [SDNPOutGlue]>;

def tmsselect : SDNode<"TMSISD::SELECT", SDT_tmsselect>;

def Wrapper : SDNode<"TMSISD::WRAPPER", SDT_Wrapper>;

def BUNDLE_END : pseudoinst<(outs), (ins), "${:comment} BUNDLE_END", []>;
def BR_OCCURS : pseudoinst<(outs), (ins), "${:comment} branch occurs", []> {
  let isTerminator = 1;
}

// XXX These should probably be marked as for codegen modelling only.
// Don't have flexibility to test right now though.
def call_start_i : inst<(outs), (ins i32imm:$val), "sub\t.D2\tB15,\t$val,\tB15",
		[(callseq_start uconst5:$val)], 1, unit_d>
{
  let Pattern = sched_pattern;
  let neverHasSideEffects = 0;
  let hasSideEffects = 1;

  let Uses = [B15];
  let Defs = [B15];
}


def call_start_r : inst<(outs), (ins GPRegs:$val), "sub\t.D2X\tB15,\t$val,\tB15"
		,[(callseq_start GPRegs:$val)], 1, unit_d>
{
  let Pattern = sched_pattern;
  let neverHasSideEffects = 0;
  let hasSideEffects = 1;

  let Uses = [B15];
  let Defs = [B15];
}

def call_end_i : inst<(outs), (ins i32imm:$val, i32imm:$val2),
		"add\t.D2\t$val,\tB15,\tB15",
		[(callseq_end timm:$val, uconst5:$val2)], 1, unit_d>
{
  let Pattern = sched_pattern;
  let neverHasSideEffects = 0;
  let hasSideEffects = 1;

  let Uses = [B15];
  let Defs = [B15];
}

def call_end_r : inst<(outs), (ins GPRegs:$val, i32imm:$val2),
		"add\t.D2X\tB15,\t$val,\tB15",
		[(callseq_end GPRegs:$val, timm:$val2)], 1, unit_d>
{
  let Pattern = sched_pattern;
  let neverHasSideEffects = 0;
  let hasSideEffects = 1;

  let Uses = [B15];
  let Defs = [B15];
}

def TMS320C64Xcall : SDNode<"TMSISD::CALL", SDT_call,
                            [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                             SDNPVariadic]>;

def retflag : SDNode<"TMSISD::RETURN_FLAG", SDTNone,
                     [SDNPHasChain, SDNPOptInGlue]>;

// FIXME: mv can be in any s/d/l slot
let neverHasSideEffects = 1 in {
def noop : inst<(outs), (ins i32imm:$cycles), "nop\t\t$cycles", [],
                                0, unit_s>;
}

// Shifts and rotates

let Supported = units_s in {
def srl_p_rr : pseudo_rr<"shru", ".S1", unit_s, srl, 0>;
def srl_p_ri : pseudo_ri<"shru", ".S1", (i32 uconst5:$imm), unit_s, srl, 0>;
}
def shl_p_rr : pseudo_rr<"shl", ".S1", unit_s, shl, 0>;
def shl_p_ri : pseudo_ri<"shl", ".S1", (i32 uconst5:$imm), unit_s, shl, 0>;
def shr_p_rr : pseudo_rr<"shr", ".S1", unit_s, sra, 0>;
def shr_p_ri : pseudo_ri<"shr", ".S1", (i32 uconst5:$imm), unit_s, sra, 0>;
def rotl_p_rr : pseudo_rr<"rotl", ".M1", unit_m, rotl, 0>;
def rotl_p_ri : pseudo_ri<"rotl", ".M1", (i32 uconst5:$imm), unit_m, rotl, 0>;

//Some arithmatic and logic

// Instr definition (spru732h p94) states that when printing we should use
// src1, src2 if the xform is used (src2), and src2, src1 if we use the xform
// and a constant.
// XXX - for unit D, depending on the form the constant can be sign extended
// or zero extended; potential for datasheet bugs or data entry bugs here
let Supported = units_notm in {
def add_p_rr : pseudo_rr<"add", ".L1", unit_l, add, 0>;
def add_p_ri : pseudo_ri<"add", ".L1", (i32 sconst5:$imm), unit_l, add, 0>;
}

let Constraints = "$src1 = $dst", AsmString = "addk\t.S1\t$imm,\t$dst" in {
def addk_p : pseudo_ri<"addk", "", (i32 sconst16:$imm), unit_s, add, 0>;
}

def and_p_rr : pseudo_rr<"and", ".L1", unit_l, and, 0>;
def and_p_ri : pseudo_ri<"and", ".L1", (i32 sconst5:$imm), unit_l, and, 1>;

def or_p_rr : pseudo_rr<"or", ".D1", unit_d, or, 0>;
def or_p_ri : pseudo_ri<"or", ".D1", (i32 sconst5:$imm), unit_d, or, 1>;

def xor_p_rr : pseudo_rr<"xor", ".D1", unit_d, xor, 0>;
def xor_p_ri : pseudo_ri<"xor", ".D1", (i32 sconst5:$imm), unit_d, xor, 1>;

def sub_p_rr : pseudo_rr<"sub", ".L1", unit_l, sub, 0>;
def sub_p_ri : pseudo_ri<"sub", ".D1", (i32 uconst5:$imm), unit_d, sub, 0>;

// Prolog asm requires some special cases:

// The actual prolog itself now requires some insns to occur in parallel,
// which I haven't yet modelled for tablegen. So instead we have this fake
// instruction that the assembly printer will see and emit the parallel code
// for. Unpleasent, but saves a whole load of pain immediately.

// the terminator is a hack to keep the postRA scheduler from moving these
// (goes away once prolog/epilog gets disentangled)
let isTerminator = 1 in {
def prolog : inst<(outs), (ins i32imm:$stacksz), "", [], 0, unit_s>;

// Similar story for epilog
def epilog : inst<(outs), (ins), "", [], 0, unit_s> {
  let Defs = [B3];
}
}

// Multiplies - there are a million and one of them, so we'll just be using
// mpy32. It might be possible to apply various constraints to the size of
// operands so that we can use 16 bit multiplies, but there's no chance of
// being able to account for different positions in registers.

// NB: mpy32 is c64x+ only

let DelaySlots = 3,
    hasDelaySlot = 1,
    Itinerary = Multiply,
    Supported = units_m in {
def mpy32_p : pseudo_rr<"mpy32", ".M1", unit_m, mul, 0>;
}

///////////////////////////////////////////////////////////////////////////////
// MOVE instructions                                                         //
///////////////////////////////////////////////////////////////////////////////

// FIXME, a move instruction, as well as a mvk or mvkh/mvkl for immediates can
// be issued on an of the S, D, L units. For the time being we assign them to
// the S unit only, resp. leave the unit selection up to the assembler

def mv : moveInstr<(outs GPRegs:$dst), (ins GPRegs:$src),
                   "mv\t$src,\t$dst", 0, unit_s>;

defm mvkl : moveLowInstr<"mvkl", "", 0>;
defm mvkh : moveHighInstr<"mvkh", "", 0>;

// the same principle here too, for gen-purpose registers leave the unit un-
// specified, for A resp. B reg files specify the execution unit explicitly.
// This will allow an easier extending in future

let isMoveImm = 1 in
{
  def mvk : moveInstr<(outs GPRegs:$dst), (ins i32imm:$imm, pred:$prd),
                      "mvk\t$imm,\t$dst", 0, unit_s>;

  def mvk_A : moveInstr<(outs ARegs:$dst), (ins i32imm:$imm, pred:$prd),
                        "mvk\t.S1\t$imm,\t$dst", 0, unit_s>;

  def mvk_B : moveInstr<(outs BRegs:$dst), (ins i32imm:$imm, pred:$prd),
                        "mvk\t.S2\t$imm,\t$dst", 0, unit_s>;
} // isMoveImm

// mvd instruction can be issued on the M unit only. The instruction as such
// is not that important, since i doubt there will be many situations where it
// would be more profitable compared to a regular non-delayed move

let hasDelaySlot = 1, DelaySlots = 3 in
{
  def mvd : moveInstr<(outs GPRegs:$dst), (ins GPRegs:$src),
                      "mvd\t.M1\t$src,\t$dst", 0, unit_m>;
}

// move appropriate immediates (signed16) directly
def : Pat<(i32 isSigned16imm:$val), (mvk imm:$val)>;

// TODO, rename to be more expressive !
def : Pat<(i32 mvk_all_pred:$val),
          (mvkh_immediate mvk_all_pred:$val,
            (mvkl_immediate mvk_all_pred:$val))>;

def : Pat<(i32 (Wrapper tglobaladdr:$val)),
          (mvkh_immediate tglobaladdr:$val,
            (mvkl_immediate tglobaladdr:$val))>;

def : Pat<(i32 (Wrapper tjumptable:$dst)),
          (mvkh_immediate tjumptable:$dst,
            (mvkl_immediate tjumptable:$dst))>;

def : Pat<(i32 (Wrapper texternalsym:$dst)),
          (mvkh_immediate texternalsym:$dst,
            (mvkl_immediate texternalsym:$dst))>;

// NKim, instruction for emitting a returning pad for an indirectly called rou-
// tine. I model this as a pseudo-instruction, since this correspond to a label.
// NOTE, we need to signal, that this pseudo-instruction may have side-effects
// in order not to be eliminated by the optimizers !

def call_return_label : pseudoinst<(outs), (ins LabelOperand:$label),
                                   "$label:\t\t${:comment} return label", []>
{
  let isCodeGenOnly = 0;
  let neverHasSideEffects = 0;
  let isPredicable = 0;
}

// NKim, we now do this, since the TI does only support signed 16 bit constant-
// quantities. To allow jumps/jsr beyond that range, we issue two instructions
// for loading the upper and a lower part of the constant into a 32 bit TI-reg

def : Pat<(call_label_operand_node texternalsym:$label),
          (mvkh_label texternalsym:$label,
            (mvkl_label texternalsym:$label))>;

def : Pat<(call_return_label_node texternalsym:$label),
          (call_return_label texternalsym:$label)>;

// FIXME: Work out what on earth to do with lea
def lea_fail : inst<(outs ARegs:$dst), (ins mem_operand:$ptr),
                "add\t.L1\t$ptr,\t$dst",
                [(set ARegs:$dst, idxaddr:$ptr)],
                0, unit_l> {
        let Pattern = sched_pattern;
}

// Memory access: we can handle all post/pre inc/dec modes, and all indexing
// situations, with a) alignment limitation, b) offset limitation,
// c) sign extension

let MemShift = 0 in {
defm u_i1 : meminst_load_p<"bu", zextloadi1>;
}

let MemShift = 0 in {
defm byte : meminst_p<"b", sextloadi8, truncstorei8>;
defm ubyte : meminst_load_p<"bu", zextloadi8>;
}

let MemShift = 1 in {
defm hword : meminst_p<"h", sextloadi16, truncstorei16>;
defm uhword : meminst_load_p<"hu", zextloadi16>;
}

let MemShift = 2 in {
defm word : meminst_p<"w", load, store>;
}

// Comparisons

def cmpeq_p_rr : pseudo_rr<"cmpeq", ".L1", unit_l, cmpeq_node, 0>;
def cmpeq_p_ri : pseudo_ri<"cmpeq", ".L1", (i32 sconst5:$imm), unit_l,
				cmpeq_node, 1>;

def : Pat<(cmpne_node GPRegs:$reg1, GPRegs:$reg2),
		(xor_p_ri (cmpeq_p_rr GPRegs:$reg1, GPRegs:$reg2), (i32 1))>;

// NOTE: Can't use any of the immediate forms of comparison instructions. LLVM
// always puts the constant operand on the right hand side of all operations,
// and TI instructions only allow constants on the left. That's fine and can
// be flipped, but that requires llvm to then go through all uses and invert
// the polarity of any tests/whatevers based on this. Not sure how to do that.
// Possibly it can't be done, we just have to handle it in some machdep fashion.

def cmpgt_p_rr : pseudo_rr<"cmpgt", ".L1", unit_l, cmpgt_node, 0>;
//def cmpgt_p_ri : pseudo_ri<"cmpgt", ".L1", (i32 sconst5:$imm), unit_l,
//						cmpgt_node, 1>;

def cmpgtu_p_rr : pseudo_rr<"cmpgtu", ".L1", unit_l, cmpgtu_node, 0>;
//def cmpgtu_p_ri : pseudo_ri<"cmpgtu", ".L1", (i32 uconst4:$imm), unit_l,
//						cmpgtu_node, 1>;

def cmplt_p_rr : pseudo_rr<"cmplt", ".L1", unit_l, cmplt_node, 0>;
//def cmplt_p_ri : pseudo_ri<"cmplt", ".L1", (i32 sconst5:$imm), unit_l,
//						cmplt_node, 1>;

def cmpltu_p_rr : pseudo_rr<"cmpltu", ".L1", unit_l, cmpltu_node, 0>;
//def cmpltu_p_ri : pseudo_ri<"cmpltu", ".L1", (i32 uconst4:$imm), unit_l,
//						cmpltu_node, 1>;

// Select - lowering loads one value into first reg, then we conditionally
// move the other value into it. Having a pair of instructions both move into
// the same register would achieve it with one cycle, but that involves touching
// parallel stuff, which we're avoiding for now

def mvselect : nopred_inst<(outs GPRegs:$dst), (ins GPRegs:$reg, GPRegs:$src1),
		"mv\t\t$reg,\t$dst", [], 0, unit_s> {
	let Constraints = "$src1 = $dst";
}

def : Pat<(tmsselect GPRegs:$false, GPRegs:$true, timm:$trueimm, predwrapper1:$truereg),
	  (mvselect GPRegs:$true, GPRegs:$false, imm:$trueimm, predwrapper1:$truereg)>;

///////////////////////////////////////////////////////////////////////////////
// BRANCH instructions                                                       //
///////////////////////////////////////////////////////////////////////////////

// NOTE, we do not yet declare the unconditional branches as being a barrier
// since there is a way of a branch being predicated, will check again later

let isBranch = 1,
    isTerminator = 1,
    DelaySlots = 5,
    hasDelaySlot = 1 in
{
  def branch : inst<(outs), (ins BranchTargetOperand:$block),
                    "b\t$block", [(br bb:$block)], 0, unit_s>
  {
    let Pattern = sched_pattern;
  }

  def branch_A : inst<(outs), (ins BranchTargetOperand:$block),
                      "b\t.S1\t$block", [(br bb:$block)], 0, unit_s>;

  def branch_B : inst<(outs), (ins BranchTargetOperand:$block),
                      "b\t.S2\t$block", [(br bb:$block)], 1, unit_s>;

  // conditional branches are being declared as non-predicated...
  // i am not sure whether the codegen respects this properly...
  def branch_cond : nopred_inst<(outs), (ins BranchTargetOperand:$block), 
                      "b\t.S1\t$block",
                       [(brcond_node bb:$block, timm:$a, predwrapper1:$b)],
                       0, unit_s>
  {
    let Pattern = sched_pattern;
  }

  // indirect (register) branches
  def branch_reg : inst<(outs), (ins GPRegs:$reg),
                        "b\t.S2X\t$reg",
                         [(brind GPRegs:$reg)], 1, unit_s>
  {
    let Pattern = sched_pattern;
  }
} // isBranch


// XXX XXX XXX: callp is a c64x+ instruction, which the assembler is unlikely
// to swallow right now. How does TI do branches-with-return-addr on c64x?
let isCall = 1, isPredicable = 0,
        Defs = [A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A16, A17, A18, A19,
                A20, A21, A22, A23, A24, A25, A26, A27, A28, A29, A30, A31,
                B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B16, B17, B18, B19,
                B20, B21, B22, B23, B24, B25, B26, B27, B28, B29, B30, B31,
    // B15: stack pointer. actually an implicit use (of an undefined reg).
    // don't know how to model that though
    B15 ] in {

// NB: Only side 2 callp instructions can be used - this is because they store
// the return address in register 3, on whichever side the callp was executed
// upon. So callp .S1 puts ret in A3, which isn't consistent with the TI
// calling convention.

// this is a call issued to an address which is globally visible
def callp_global : inst<(outs), (ins CallTargetOperand:$dst, variable_ops),
                        "callp\t.S2\t$dst,\tB3",
                        [(TMS320C64Xcall tglobaladdr:$dst)], 1, unit_s>
{
  let Pattern = sched_pattern;
}

// this is a call issued to a symbol which is defined externally
def callp_extsym : inst<(outs), (ins CallTargetOperand:$dst, variable_ops),
                        "callp\t.S2\t$dst,\tB3",
                        [(TMS320C64Xcall texternalsym:$dst)], 1, unit_s>
{
  let Pattern = sched_pattern;
}

// NKim, indirect call, this actually produces the same code as an indirect
// branch, however, the semantics are slightly different as you can think
def call_reg : inst<(outs), (ins GPRegs:$reg, variable_ops),
                    "b\t.S2X\t$reg",
                    [(TMS320C64Xcall GPRegs:$reg)], 1, unit_s>
{
  let isBranch = 1;
  let Itinerary = Branch;
  let DelaySlots = 5;
  let hasDelaySlot = 1;

  let Pattern = sched_pattern;
  let Uses = [B3];
}
} // isCall

// NB: branch-to-register can only work on unit S2
// Epilogue code should juggle the return addr into B3.
// We always put the branch on this side.
def ret : inst<(outs), (ins), "b\t.S2\tB3", [(retflag)], 1, unit_s> {
        let isReturn = 1;
        let isTerminator = 1;
        let isPredicable = 0;
        let Pattern = sched_pattern;

        let Uses = [B3]; // the return address

  let Itinerary = Branch;
  let hasDelaySlot = 1;
  let DelaySlots = 5;
}

let Supported = units_notm in {
  defm add_rr : c64inst<(ins GPRegs:$src2), "add">;
  defm add_ri : c64iinst<(ins i32imm:$src2), "add">;
}

let Supported = units_s in {
  defm srl_rr : c64swinst<(ins GPRegs:$src1), "shru">;
  defm srl_ri : c64iinst<(ins i32imm:$src2), "shru">;
}

// NKIM, not sure whether this works properly
let InOperandList = (ins i32imm:$src2, pred:$s, form:$fu),
    AsmString = "mvk\t.$fu\t$src2,\t$dst",
    Supported = units_s in {
  defm mvk : c64inst<(ins), "mvk">;
}

/* stuff prior to llvm-2.9
let InOperandList = (ops i32imm:$src2, pred:$s, form:$fu),
    AsmString = "mvk\t.$fu\t$src2,\t$dst",
    Supported = units_s in {
  defm mvk : c64inst<(ins), "mvk">;
}
*/

let DelaySlots = 3,
    hasDelaySlot = 1,
    Itinerary = Multiply,
    Supported = units_m in {
  defm mpy32 : c64inst<(ins GPRegs:$src2), "mpy32">;
}

let MemShift = 0 in {
  defm byte  : c64strictload<"b">;
  defm byte  : c64store<"b">;
  defm ubyte : c64strictload<"bu">;
}

let MemShift = 1 in {
  defm hword  : c64strictload<"h">;
  defm hword  : c64store<"h">;
  defm uhword : c64strictload<"hu">;
}

let MemShift = 2 in {
  defm word : c64load<"w">;
  defm word : c64strictload<"w">;
  defm word : c64store<"w">;
}
