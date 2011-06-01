//===-- TMS320C64XLowering.cpp - TMS320C64X DAG Lowering Implementation  --===//
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

#include "TMS320C64XLowering.h"
#include "TMS320C64XTargetMachine.h"
#include "TMS320C64XTargetObjectFile.h"

#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"
#include "llvm/CallingConv.h"
#include "llvm/GlobalVariable.h"
#include "llvm/GlobalAlias.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CallingConv.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/MC/MCSymbol.h"

using namespace llvm;

static bool CC_TMS320C64X_Custom(unsigned &ValNo,
                                 MVT &ValVT,
                                 MVT &LocVT,
                                 CCValAssign::LocInfo &LocInfo,
                                 ISD::ArgFlagsTy &ArgFlags,
                                 CCState &State)
{
  // list of all argument registers in pairs
  static const unsigned AllRegs[] = {
     TMS320C64X::A4, TMS320C64X::A5,
     TMS320C64X::B4, TMS320C64X::B5,
     TMS320C64X::A6, TMS320C64X::A7,
     TMS320C64X::B6, TMS320C64X::B7,
     TMS320C64X::A8, TMS320C64X::A9,
     TMS320C64X::B8, TMS320C64X::B9,
     TMS320C64X::A10, TMS320C64X::A11,
     TMS320C64X::B10, TMS320C64X::B11,
     TMS320C64X::A12, TMS320C64X::A13,
     TMS320C64X::B12, TMS320C64X::B13
  };

  // for i32/f32 values the 2nd reg in the pair is shadowed
  // note: odd regs shadow themselves, thus no effect
  static const unsigned ShadowRegs[] = {
    TMS320C64X::A5, TMS320C64X::A5,
    TMS320C64X::B5, TMS320C64X::B5,
    TMS320C64X::A7, TMS320C64X::A7,
    TMS320C64X::B7, TMS320C64X::B7,
    TMS320C64X::A9, TMS320C64X::A9,
    TMS320C64X::B9, TMS320C64X::B9,
    TMS320C64X::A11, TMS320C64X::A11,
    TMS320C64X::B11, TMS320C64X::B11,
    TMS320C64X::A13, TMS320C64X::A13,
    TMS320C64X::B13, TMS320C64X::B13
  };

  // just the odd ones to identify the 2nd reg of a pair
  static const unsigned LoRegs[] = {
    TMS320C64X::A5, TMS320C64X::B5,
    TMS320C64X::A7, TMS320C64X::B7,
    TMS320C64X::A9, TMS320C64X::B9,
    TMS320C64X::A11, TMS320C64X::B11,
    TMS320C64X::A13, TMS320C64X::B13
  };

  unsigned Reg = 0;
  if (ArgFlags.isSplit())
    // first reg of a split i64/f64 - allocate but don't shadow
    Reg = State.AllocateReg(AllRegs, array_lengthof(AllRegs));
  else
    // normal argument or 2nd part of split - allocate with shadow reg
    Reg = State.AllocateReg(
      AllRegs, ShadowRegs, array_lengthof(AllRegs));

  if (!Reg)
    return false;
  else if (ArgFlags.isSplit()
      || std::count(&LoRegs[0], &LoRegs[array_lengthof(LoRegs)], Reg))
    // part of a split value - make this a custom reg
    State.addLoc(
      CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    State.addLoc(
      CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));

  return true;
}

#include "TMS320C64XGenCallingConv.inc"

//-----------------------------------------------------------------------------

TMS320C64XLowering::TMS320C64XLowering(TargetMachine &tm)
: TargetLowering(tm, new TMS320C64XTargetObjectFile())
{
  // Bool is represented in i32 by 0 or 1
  setBooleanContents(ZeroOrOneBooleanContent);

  addRegisterClass(MVT::i32, TMS320C64X::GPRegsRegisterClass);
  addRegisterClass(MVT::i32, TMS320C64X::ARegsRegisterClass);
  addRegisterClass(MVT::i32, TMS320C64X::BRegsRegisterClass);
  addRegisterClass(MVT::i32, TMS320C64X::PredRegsRegisterClass);

  setLoadExtAction(ISD::SEXTLOAD, MVT::i1, Promote);
  setLoadExtAction(ISD::EXTLOAD, MVT::i1, Custom);
  setLoadExtAction(ISD::EXTLOAD, MVT::i8, Custom);
  setLoadExtAction(ISD::EXTLOAD, MVT::i16, Custom);
  setLoadExtAction(ISD::EXTLOAD, MVT::i32, Custom);

  // All other loads have sx and zx support

  // No 32 bit load insn
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  // No in-reg sx
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // No divide anything
  setOperationAction(ISD::UDIV, MVT::i32, Expand);
  setOperationAction(ISD::SDIV, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);

  // On dspbridge, divides and remainders are implemented by these two
  // routines. Possibly in the future we should check the target triple
  // to see whether we're targeting something with a real libc, and make
  // a choice about what divide/modulus libcalls to make. Until then,
  // assume we're always on dspbridge.
  setLibcallName(RTLIB::UDIV_I32, "__divu");
  setLibcallName(RTLIB::UREM_I32, "__remu");

  // Support for FP lib from TI
  //
  // Single-precision floating-point arithmetic.
  setLibcallName(RTLIB::ADD_F32, "__addf");
  setLibcallName(RTLIB::SUB_F32, "__subf");
  setLibcallName(RTLIB::MUL_F32, "__mpyf");
  setLibcallName(RTLIB::DIV_F32, "__divf");

  // Double-precision floating-point arithmetic.
  setLibcallName(RTLIB::ADD_F64, "__addd");
  setLibcallName(RTLIB::SUB_F64, "__subd");
  setLibcallName(RTLIB::MUL_F64, "__mpyd");
  setLibcallName(RTLIB::DIV_F64, "__divd");

  // Single-precision comparisons.
  setLibcallName(RTLIB::OEQ_F32, "__cmpf");
  setLibcallName(RTLIB::UNE_F32, "__cmpf");
  setLibcallName(RTLIB::OLT_F32, "__cmpf");
  setLibcallName(RTLIB::OLE_F32, "__cmpf");
  setLibcallName(RTLIB::OGE_F32, "__cmpf");
  setLibcallName(RTLIB::OGT_F32, "__cmpf");
  setLibcallName(RTLIB::UO_F32,  "__not_implemented");
  setLibcallName(RTLIB::O_F32,   "__not_implemented");

  setCmpLibcallCC(RTLIB::OEQ_F32, ISD::SETEQ);
  setCmpLibcallCC(RTLIB::UNE_F32, ISD::SETNE);
  setCmpLibcallCC(RTLIB::OLT_F32, ISD::SETLT);
  setCmpLibcallCC(RTLIB::OLE_F32, ISD::SETLE);
  setCmpLibcallCC(RTLIB::OGE_F32, ISD::SETGE);
  setCmpLibcallCC(RTLIB::OGT_F32, ISD::SETGT);

  // Double-precision comparisons.
  setLibcallName(RTLIB::OEQ_F64, "__cmpd");
  setLibcallName(RTLIB::UNE_F64, "__cmpd");
  setLibcallName(RTLIB::OLT_F64, "__cmpd");
  setLibcallName(RTLIB::OLE_F64, "__cmpd");
  setLibcallName(RTLIB::OGE_F64, "__cmpd");
  setLibcallName(RTLIB::OGT_F64, "__cmpd");
  setLibcallName(RTLIB::UO_F64,  "_not_implemented");
  setLibcallName(RTLIB::O_F64,   "_not_implemented");

  setCmpLibcallCC(RTLIB::OEQ_F64, ISD::SETEQ);
  setCmpLibcallCC(RTLIB::UNE_F64, ISD::SETNE);
  setCmpLibcallCC(RTLIB::OLT_F64, ISD::SETLT);
  setCmpLibcallCC(RTLIB::OLE_F64, ISD::SETLE);
  setCmpLibcallCC(RTLIB::OGE_F64, ISD::SETGE);
  setCmpLibcallCC(RTLIB::OGT_F64, ISD::SETGT);

  // Conversions between floating types.
  setLibcallName(RTLIB::FPROUND_F64_F32, "__cvtdf");
  setLibcallName(RTLIB::FPEXT_F32_F64,   "__cvtfd");

  // Floating-point to integer conversions.
  setLibcallName(RTLIB::FPTOSINT_F64_I32, "__fixdi");
  setLibcallName(RTLIB::FPTOUINT_F64_I32, "__fixdu");
  setLibcallName(RTLIB::FPTOSINT_F32_I32, "__fixfi");
  setLibcallName(RTLIB::FPTOUINT_F32_I32, "__fixfu");

  // Integer to floating-point conversions.
  setLibcallName(RTLIB::SINTTOFP_I32_F64, "__fltid");
  setLibcallName(RTLIB::SINTTOFP_I32_F32, "__fltif");
  setLibcallName(RTLIB::UINTTOFP_I32_F64, "__fltud");
  setLibcallName(RTLIB::UINTTOFP_I32_F32, "__fltuf");

  // We can generate two conditional instructions for select, not so
  // easy for select_cc
  setOperationAction(ISD::SELECT, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);

  // Manually beat condition code setting into cmps
  setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // We can emulate br_cc, maybe not brcond, do what works
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::i32, Expand);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // Probably is a membarrier, but I'm not aware of it right now
  setOperationAction(ISD::MEMBARRIER, MVT::Other, Expand);

  // Should also inject other invalid operations here
  // The following might be supported, but I can't be bothered or don't
  // have enough time to work on
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);

  // There's no carry support, we're supposed to use 40 bit integers
  // instead. Let llvm generate workarounds instead.
  setOperationAction(ISD::ADDC, MVT::i32, Expand);
  setOperationAction(ISD::SUBC, MVT::i32, Expand);
  setOperationAction(ISD::ADDE, MVT::i32, Expand);
  setOperationAction(ISD::SUBE, MVT::i32, Expand);

  // VACOPY and VAEND apparently have sane defaults, however
  // VASTART and VAARG can't be expanded
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i1, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i16, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i32, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  setOperationAction(ISD::INTRINSIC_VOID, MVT::i1, Custom);

  setStackPointerRegisterToSaveRestore(TMS320C64X::A15);
  computeRegisterProperties();

  // We want BURR for prepass scheduling as we sometimes break the DAG with
  // cycles and TD doesn't assert on that.
  setSchedulingPreference(SchedulingForRegPressure);
}

//-----------------------------------------------------------------------------

TMS320C64XLowering::~TMS320C64XLowering() {}

//-----------------------------------------------------------------------------

const char *TMS320C64XLowering::getTargetNodeName(unsigned op) const {
  switch (op) {
    default: return NULL;
    case TMSISD::BRCOND:
      return "TMSISD::BRCOND";

    case TMSISD::CALL:
      return "TMSISD::CALL";

    case TMSISD::CMPEQ:
      return "TMSISD::CMPEQ";

    case TMSISD::CMPNE:
      return "TMSISD::CMPNE";

    case TMSISD::CMPGT:
      return "TMSISD::CMPGT";

    case TMSISD::CMPGTU:
      return "TMSISD::CMPGTU";

    case TMSISD::CMPLT:
      return "TMSISD::CMPLT";

    case TMSISD::CMPLTU:
      return "TMSISD::CMPLTU";

    case TMSISD::RETURN_FLAG:
      return "TMSISD::RETURN_FLAG";

    case TMSISD::RETURN_LABEL:
      return "TMSISD::RETURN_LABEL";

    case TMSISD::RETURN_LABEL_OPERAND:
      return "TMSISD::RETURN_LABEL_OPERAND";

    case TMSISD::SELECT:
      return "TMSISD::SELECT";

    case TMSISD::WRAPPER:
      return "TMSISD::WRAPPER";

    case TMSISD::PRED_STORE:
      return "TMSISD::PRED_STORE";
  }
}

//-----------------------------------------------------------------------------

unsigned TMS320C64XLowering::getFunctionAlignment(Function const*) const {
  return 5; // 32 bytes; instruction packet
}

//-----------------------------------------------------------------------------

using namespace TMS320C64X;

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerFormalArguments(SDValue Chain,
                                         CallingConv::ID CallConv,
                                         bool isVarArg,
				         const SmallVectorImpl<ISD::InputArg> &Ins,
                                         DebugLoc dl,
                                         SelectionDAG &DAG,
                                         SmallVectorImpl<SDValue> &InVals) const
{
  SmallVector<CCValAssign, 16> ArgLocs;
  unsigned int reg;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  unsigned arg_idx = 0;
  unsigned stack_offset = 4; // Arguments start 4 bytes before the FP

  CCState CCInfo(
    CallConv, isVarArg, getTargetMachine(), ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeFormalArguments(Ins, CC_TMS320C64X);

  // TI calling convention dictates that the argument preceeding the
  // variable list of arguments must also be placed on the stack.
  // This doesn't cause a problem as C dictates there must always be one
  // non-var argument. But in any case, we need to break out of the
  // register-munging loop and ensure the last fixed argument goes to
  // the stack.
  unsigned last_fixed_arg = ArgLocs.size() - 1;

  // Ditch location allocation of arguments and do our own thing - only
  // way to make varargs work correctly
  for (unsigned i = 0; i < ArgLocs.size(); ++i) {

    CCValAssign &VA = ArgLocs[i];
    EVT ObjectVT = VA.getValVT();

    switch (ObjectVT.getSimpleVT().SimpleTy) {
      default: llvm_unreachable("Unhandled argument type");
      case MVT::i1:
      case MVT::i8:
      case MVT::i16:
      case MVT::i32:
        if (!Ins[i].Used && (!isVarArg || i != last_fixed_arg)) {
          if (arg_idx < 10) arg_idx++;

          InVals.push_back(DAG.getUNDEF(ObjectVT));
        }
        else if (arg_idx < 10 && (!isVarArg || i != last_fixed_arg)) {
          // args in registers have correct locations
          // from CC_TMS320C64X_Custom
          unsigned arg_reg = VA.getLocReg();
          reg = RegInfo.createVirtualRegister(&GPRegsRegClass);
          MF.getRegInfo().addLiveIn(arg_reg, reg);
          SDValue Arg = DAG.getCopyFromReg(Chain, dl, reg, MVT::i32);

          if (ObjectVT != MVT::i32) {
            Arg = DAG.getNode(
              ISD::AssertSext, dl, MVT::i32, Arg, DAG.getValueType(ObjectVT));

            Arg = DAG.getNode(ISD::TRUNCATE, dl, ObjectVT, Arg);
          }

          InVals.push_back(Arg);
          arg_idx++;
        }
        else {
          // XXX - i64?
          int frame_idx = MFI->CreateFixedObject(4, stack_offset, true);

          SDValue FIPtr = DAG.getFrameIndex(frame_idx, MVT::i32);
          SDValue load;

          MachinePointerInfo MPI;

          if (ObjectVT == MVT::i32) {
            // XXX - Non temporal? Eh?
            load = DAG.getLoad(
              MVT::i32, dl, Chain, FIPtr, MPI, false, false, 4);
          }
          else {
            // XXX - work out alignment
            load = DAG.getExtLoad(ISD::SEXTLOAD, dl, MVT::i32,
              Chain, FIPtr, MPI, ObjectVT, false, false, 4);

            load = DAG.getNode(ISD::TRUNCATE, dl, ObjectVT, load);
          }

          InVals.push_back(load);
          stack_offset += 4;
        }
    } // switch
  } // argloop

  return Chain;
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerReturn(SDValue Chain,
                                CallingConv::ID CallConv,
				bool isVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                DebugLoc dl,
                                SelectionDAG &DAG) const
{
  SmallVector<CCValAssign, 16> RLocs;
  SDValue Flag;

  CCState CCInfo(
    CallConv, isVarArg, DAG.getTarget(), RLocs, *DAG.getContext());

  CCInfo.AnalyzeReturn(Outs, RetCC_TMS320C64X);

  // Apparently we need to add this to the out list only if it's first
  if (DAG.getMachineFunction().getRegInfo().liveout_empty()) {
    for (unsigned i = 0; i != RLocs.size(); ++i)
      if (RLocs[i].isRegLoc())
        DAG.getMachineFunction().getRegInfo().addLiveOut
	  (RLocs[i].getLocReg());
  }

  for (unsigned i = 0; i != RLocs.size(); ++i) {
    CCValAssign &VA = RLocs[i];
    assert(VA.isRegLoc() && "Invalid return position");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Flag);
    Flag = Chain.getValue(1);
  }

  if (Flag.getNode())
    return DAG.getNode(TMSISD::RETURN_FLAG, dl, MVT::Other, Chain, Flag);
  else return DAG.getNode(TMSISD::RETURN_FLAG, dl, MVT::Other, Chain);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerCall(SDValue Chain,
                                      SDValue Callee,
                                      CallingConv::ID CallConv,
                                      bool isVarArg,
                                      bool &isTailCall,
                                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                                      const SmallVectorImpl<SDValue> &OutVals,
                                      const SmallVectorImpl<ISD::InputArg> &Ins,
                                      DebugLoc dl,
                                      SelectionDAG &DAG,
                                      SmallVectorImpl<SDValue> &InVals) const
{
  SmallVector<CCValAssign, 16> ArgLocs;
  unsigned arg_idx, fixed_args;

  arg_idx = 0;
  fixed_args = 0;

  isTailCall = false;

  CCState CCInfo(
    CallConv, isVarArg, getTargetMachine(), ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeCallOperands(Outs, CC_TMS320C64X);

  for (unsigned i = 0; i < Outs.size(); i++) {
    if (Outs[i].IsFixed) fixed_args++;
  }

  int stacksize, bytes = 0;

  // Make our own stack and register decisions; however keep CCInfos
  // thoughts on sign extension, as they're handy.
  // Start out by guessing how much stack space we need
  if (!isVarArg) {
    if (ArgLocs.size() > 10) {
      stacksize = (ArgLocs.size() - 10) * 4; // XXX - i64?
    }
    else stacksize = 0;
  }
  else stacksize = (ArgLocs.size() - fixed_args + 1) * 4;

  // Round the amount of stack we allocate to hold stack-arguments up to
  // an 8 byte alignment - it turns out the stack on the DSP side needs
  // to remain dword aligned.
  stacksize = (stacksize + 7) & ~7;

  Chain = DAG.getCALLSEQ_START(Chain, DAG.getConstant(stacksize, MVT::i32));

  SmallVector<std::pair<unsigned int, SDValue>, 16> reg_args;
  SmallVector<SDValue, 16> stack_args;

  for (unsigned i = 0; i < ArgLocs.size(); ++i) {

    CCValAssign &va = ArgLocs[i];
    SDValue arg = OutVals[i];

    switch (va.getLocInfo()) {
      default: llvm_unreachable("Unknown arg loc");
      case CCValAssign::Full:
        break;
      case CCValAssign::SExt:
        arg = DAG.getNode(ISD::SIGN_EXTEND, dl, va.getLocVT(), arg);
        break;
      case CCValAssign::ZExt:
        arg = DAG.getNode(ISD::ZERO_EXTEND, dl, va.getLocVT(), arg);
        break;
      case CCValAssign::AExt:
        arg = DAG.getNode(ISD::ANY_EXTEND, dl,  va.getLocVT(), arg);
        break;
    }

    if (arg_idx < 10 && (!isVarArg || i < fixed_args - 1)) {
      // Additional check to ensure last fixed param and all
      // variable params go on stack, if we're vararging.

      // args in registers have correct locations from CC_TMS320C64X_Custom
      unsigned reg = va.getLocReg();
      reg_args.push_back(std::make_pair(reg, arg));
      arg_idx++;
    }
    else {
      bytes += 4; /* Offset from SP at call */

      SDValue stack_ptr = DAG.getCopyFromReg(
        Chain, dl, TMS320C64X::B15, getPointerTy());

      SDValue addr = DAG.getNode(
        ISD::ADD, dl, getPointerTy(), stack_ptr, DAG.getIntPtrConstant(bytes));

      SDValue store = DAG.getStore(
        Chain, dl, arg, addr, MachinePointerInfo(), false, false, 4);

      stack_args.push_back(store);
    }
  }

  // We now have two sets of register / stack locations to load stuff
  // into; apparently we can put the memory location ones into one big
  // chain, because they can happen independently

  SDValue InFlag;

  if (!stack_args.empty()) {
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                        &stack_args[0], stack_args.size());
  }

  // This chains loading to specified registers sequentially
  for (unsigned i = 0; i < reg_args.size(); ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, reg_args[i].first,
                             reg_args[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Following what MSP430 does, convert GlobalAddress nodes to
  // TargetGlobalAddresses.
  if (GlobalAddressSDNode *g = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(
      g->getGlobal(), dl, MVT::i32, g->getOffset());
  }
  else if (ExternalSymbolSDNode *e = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(e->getSymbol(), MVT::i32);
  }

  // Tie this all into a "call", return a chain and a glue
  SDVTList OperandTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 16> ops;
  ops.push_back(Chain);
  ops.push_back(Callee);

  for (unsigned i = 0; i < reg_args.size(); ++i) {
    ops.push_back(DAG.getRegister(reg_args[i].first,
                  reg_args[i].second.getValueType()));
  }

  if (InFlag.getNode()) ops.push_back(InFlag);

  const bool isIndirectCall = (
    Callee.getOpcode() != ISD::TargetGlobalAddress &&
    Callee.getOpcode() != ISD::GlobalAddress &&
    Callee.getOpcode() != ISD::TargetExternalSymbol &&
    Callee.getOpcode() != ISD::ExternalSymbol);

  if (isIndirectCall) {

    // Unfortunately there's no direct call instruction for this. We have to
    // emulate it. That involves sticking a label directly after the call/
    // branch, and loading its address into B3 directly beforehand. Ugh...

    MachineFunction &MF = DAG.getMachineFunction();
    MachineModuleInfo &MMI = MF.getMMI();

    // generate a unique label for the return from the call, the address of
    // the label will be put into B3 and supplied to the callee to be able
    // to return from the subroutine
    MCSymbol *indirectCallOperand = MMI.getContext().CreateTempSymbol();
    const char *retLabName = indirectCallOperand->getName().data();

    SDValue glueVal = Chain.getValue(0);
    SDVTList NodeTys = DAG.getVTList(MVT::i32, MVT::Other, MVT::Glue);

    // NKim, HACK, mark the node as an external symbol for the time being.
    // This node references the external symbol as a label operand, has a
    // glue-value and also extends and propagates the chain-value
    // AJO: note the target flag '1' is being passed to tell a symbol apart from
    // a label.
    Chain = DAG.getNode(TMSISD::RETURN_LABEL_OPERAND, dl, NodeTys, Chain,
      DAG.getTargetExternalSymbol(retLabName, getPointerTy(), 1), glueVal);

    // to be sure, that the immediate (the label address) is going to land
    // in B3 we also insert a copy-to-reg node, which has three operands,
    // Value 0 is the source value, Value 1 the chain and Value 2 the glue
    Chain = DAG.getCopyToReg(Chain.getValue(1), dl, TMS320C64X::B3,
                             Chain.getValue(0), Chain.getValue(2));

    // the previously inserted node does provide a chain and a glue
    assert(Chain.getValue(0).getNode() && Chain.getValue(1).getNode() &&
      "Bad chain values detected for the indirect call DAG node!");

    ops.push_back(Chain.getValue(1));
    Chain = DAG.getNode(TMSISD::CALL, dl, OperandTys, &ops[0], ops.size());

    // now we have to mark the node apropriately to avoid funny surprises
    // (such as machine-dce removing the label, etc.)
    NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    Chain = DAG.getNode(TMSISD::RETURN_LABEL, dl, NodeTys, Chain,
      DAG.getTargetExternalSymbol(retLabName, getPointerTy(), 0),
      Chain.getValue(1));
  }
  else {
    // now insert a regular call node for the non-register calls
    Chain = DAG.getNode(TMSISD::CALL, dl, OperandTys, &ops[0], ops.size());
  }

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getConstant(stacksize, MVT::i32),
    DAG.getTargetConstant(0, MVT::i32), Chain.getValue(1));

  return LowerCallResult(
    Chain, Chain.getValue(1), CallConv, isVarArg, Ins, dl, DAG, InVals);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerCallResult(SDValue Chain,
                                            SDValue InFlag,
                                            CallingConv::ID CallConv,
                                            bool isVarArg,
                                            const SmallVectorImpl<ISD::InputArg> &Ins,
                                            DebugLoc dl,
                                            SelectionDAG &DAG,
                                            SmallVectorImpl<SDValue> &InVals) const
{
  SmallVector<CCValAssign, 16> ret_locs;
  CCState CCInfo(
    CallConv, isVarArg, getTargetMachine(), ret_locs, *DAG.getContext());

  CCInfo.AnalyzeCallResult(Ins, RetCC_TMS320C64X);

  for (unsigned i = 0; i < ret_locs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, ret_locs[i].getLocReg(),
                                          ret_locs[i].getValVT(),
                                          InFlag).getValue(1);

    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerOperation(SDValue op,  SelectionDAG &DAG) const {

  switch (op.getOpcode()) {
    case ISD::GlobalAddress:
      return LowerGlobalAddress(op, DAG);
    case ISD::JumpTable:
      return LowerJumpTable(op, DAG);
    case ISD::RETURNADDR:
      return LowerReturnAddr(op, DAG);
    case ISD::BR_CC:
      return LowerBRCC(op, DAG);
    case ISD::SETCC:
      return LowerSETCC(op, DAG);

    // We only ever get custom loads when it's an extload
    case ISD::LOAD:
      return LowerExtLoad(op, DAG);
    case ISD::SELECT:
      return LowerSelect(op, DAG);
    case ISD::VASTART:
      return LowerVASTART(op, DAG);
    case ISD::VAARG:
      return LowerVAARG(op, DAG);
    case ISD::INTRINSIC_WO_CHAIN:
      return LowerIfConv(op, DAG);
    case ISD::INTRINSIC_VOID:
      return LowerIntrinsicVoid(op, DAG);
    default:
      llvm_unreachable(op.getNode()->getOperationName().c_str());
  }

  return op;
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerGlobalAddress(SDValue op, SelectionDAG &DAG) const
{
  const GlobalValue *GV = cast<GlobalAddressSDNode>(op)->getGlobal();
  int64_t offset = cast<GlobalAddressSDNode>(op)->getOffset();

  SDValue res = DAG.getTargetGlobalAddress(
    GV, op->getDebugLoc(), getPointerTy(), offset);

  return DAG.getNode(TMSISD::WRAPPER, op.getDebugLoc(), getPointerTy(), res);
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerJumpTable(SDValue op, SelectionDAG &DAG) const {

  const JumpTableSDNode *j = cast<JumpTableSDNode>(op);

  SDValue res = DAG.getTargetJumpTable(j->getIndex(), getPointerTy(), 0);
  return DAG.getNode(TMSISD::WRAPPER, op.getDebugLoc(), getPointerTy(),	res);
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerExtLoad(SDValue op, SelectionDAG &DAG) const {

  const LoadSDNode *l = cast<LoadSDNode>(op);
  SDVTList list = l->getVTList();

  return DAG.getExtLoad(ISD::SEXTLOAD, op.getDebugLoc(), list.VTs[0],
                        l->getOperand(0), l->getOperand(1),
                        l->getPointerInfo(), l->getMemoryVT(),
			l->isVolatile(), false, l->getAlignment());
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerReturnAddr(SDValue op, SelectionDAG &DAG) const {

  if (cast<ConstantSDNode>(op.getOperand(0))->getZExtValue() != 0)
   llvm_unreachable("LowerReturnAddr -> abnormal depth");

  // Right now, we always store ret addr -> slot 0.
  // Although it could be offset by something, not certain
  SDValue retaddr = DAG.getFrameIndex(0, getPointerTy());

  return DAG.getLoad(getPointerTy(), op.getDebugLoc(), DAG.getEntryNode(),
                     retaddr, MachinePointerInfo(), false, false, 4);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerBRCC(SDValue op, SelectionDAG &DAG) const {
  int pred = 1;
  ISD::CondCode cc = cast<CondCodeSDNode>(op.getOperand(1))->get();

  // Can't do setne: instead invert predicate
  if (cc == ISD::SETNE || cc== ISD::SETUNE) {
    cc = ISD::SETEQ;
    pred = 0;
  }

  SDValue Chain = DAG.getSetCC(op.getDebugLoc(), MVT::i32,
			op.getOperand(2), op.getOperand(3), cc);

  if (Chain.getNode()->getOpcode() != ISD::Constant) {
    bool negateCC;
    tie(Chain, negateCC) = ConvertSETCC(Chain, DAG);
    if (negateCC) pred = 0;
  }

  // Generate our own brcond form, operands BB, const/reg for predicate
  Chain = DAG.getNode(TMSISD::BRCOND, op.getDebugLoc(), MVT::Other,
                      op.getOperand(0), op.getOperand(4),
		      DAG.getTargetConstant(pred, MVT::i32), Chain);
  return Chain;
}

//-----------------------------------------------------------------------------

SDValue
TMS320C64XLowering::LowerSETCC(SDValue op, SelectionDAG &DAG) const {
  SDValue Chain;
  bool NegateCC;
  tie(Chain, NegateCC) = ConvertSETCC(op, DAG);

  if (NegateCC)
    Chain = DAG.getNode(
      ISD::XOR, op.getDebugLoc(), MVT::i32, Chain, DAG.getConstant(1, MVT::i32));

  return Chain;
}

//-----------------------------------------------------------------------------

std::pair<SDValue,bool>
TMS320C64XLowering::ConvertSETCC(SDValue op, SelectionDAG &DAG) const {

  unsigned int opcode;
  bool NegateCC = false;

  SDValue lhs = PromoteBoolean(op.getOperand(0), DAG);
  SDValue rhs = PromoteBoolean(op.getOperand(1), DAG);

  ISD::CondCode cc = cast<CondCodeSDNode>(op.getOperand(2))->get();

  switch (cc) {
    case ISD::SETFALSE:
    case ISD::SETTRUE:
    case ISD::SETFALSE2:
    case ISD::SETTRUE2:
    case ISD::SETCC_INVALID:
    case ISD::SETOEQ:
    case ISD::SETOGT:
    case ISD::SETOGE:
    case ISD::SETOLT:
    case ISD::SETOLE:
    case ISD::SETONE:
    case ISD::SETO:
    case ISD::SETUO:
    default:
      llvm_unreachable("Unsupported condcode");

    case ISD::SETEQ:
    case ISD::SETUEQ:
      opcode = TMSISD::CMPEQ;
      break;
    case ISD::SETUGT:
      opcode = TMSISD::CMPGTU;
      break;
    case ISD::SETUGE:
      opcode = TMSISD::CMPLTU;
      NegateCC = true;
      break;
    case ISD::SETULT:
      opcode = TMSISD::CMPLTU;
      break;
    case ISD::SETULE:
      opcode = TMSISD::CMPGTU;
      NegateCC = true;
      break;
    case ISD::SETUNE:
      opcode = TMSISD::CMPNE;
      break;
    case ISD::SETGT:
      opcode = TMSISD::CMPGT;
      break;
    case ISD::SETGE:
      opcode = TMSISD::CMPLT;
      NegateCC = true;
      break;
    case ISD::SETLT:
      opcode = TMSISD::CMPLT;
      break;
    case ISD::SETLE:
      opcode = TMSISD::CMPGT;
      NegateCC = true;
      break;
    case ISD::SETNE:
      opcode = TMSISD::CMPNE;
      break;
  }

  SDValue Chain = DAG.getNode(opcode, op.getDebugLoc(), MVT::i32, lhs, rhs);
  return std::make_pair(Chain, NegateCC);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerSelect(SDValue op, SelectionDAG &DAG) const {
  SDValue ops[4];

  // Operand 1 is true/false, selects operand 2 or 3 respectively
  // We'll generate this with two conditional move instructions - moving
  // the true/false result to the same register. In theory these could
  // be scheduled in the same insn packet (given that only one will
  // execute out of the pair, due to the conditional)

  ops[0] = op.getOperand(1);
  ops[1] = op.getOperand(2);
  ops[2] = DAG.getTargetConstant(0, MVT::i32);
  ops[3] = op.getOperand(0);
  return DAG.getNode(TMSISD::SELECT, op.getDebugLoc(), MVT::i32, ops, 4);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerVASTART(SDValue op, SelectionDAG &DAG) const {

  int num_normal_params =
    DAG.getMachineFunction().getFunction()->getFunctionType()->getNumParams();

  int stackgap;

  // As referenced elsewhere, TI specify the last fixed argument has to
  // go on the stack. Also, any other overflow.
  if (num_normal_params <= 10) stackgap = 4;
  else stackgap = (num_normal_params - 10) * 4;

  // That gives us the offset of the last fixed stack argument; now
  // increment that to point at the first var argument
  stackgap += 4;

  SDValue Chain = DAG.getNode(ISD::ADD, op.getDebugLoc(), MVT::i32,
				DAG.getRegister(TMS320C64X::A15, MVT::i32),
				DAG.getConstant(stackgap, MVT::i32));
  const Value *SV = cast<SrcValueSDNode>(op.getOperand(2))->getValue();

  return DAG.getStore(op.getOperand(0), op.getDebugLoc(),
    Chain, op.getOperand(1), MachinePointerInfo(SV), false, false, 4);
}

//-----------------------------------------------------------------------------

SDValue TMS320C64XLowering::LowerVAARG(SDValue op, SelectionDAG &DAG) const {

  // Largely copy sparc
  SDNode *n = op.getNode();
  EVT vt = n->getValueType(0);
  SDValue chain = n->getOperand(0);
  SDValue valoc = n->getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(n->getOperand(2))->getValue();

  // Load point to vaarg list
//    MVT::i32, op.getDebugLoc(), chain, valoc, SV, 0, false, false, 4);
  SDValue loadptr = DAG.getLoad(MVT::i32, op.getDebugLoc(),
    chain, valoc, MachinePointerInfo(SV), false, false, 4);

  // Calculate address of next vaarg
  SDValue newptr = DAG.getNode(ISD::ADD, op.getDebugLoc(), MVT::i32,
    loadptr, DAG.getConstant(vt.getSizeInBits()/8, MVT::i32));

  // Store that back to wherever we're storing the vaarg list
//    loadptr.getValue(1), op.getDebugLoc(), newptr, valoc, SV, 0, false, false, 4);
  chain = DAG.getStore(loadptr.getValue(1), op.getDebugLoc(),
    newptr, valoc, MachinePointerInfo(SV), false, false, 4);

  // Actually load the argument
  return DAG.getLoad(
    vt, op.getDebugLoc(), chain, loadptr, MachinePointerInfo(), false, false, 4);
}

// This lowers the INTRINSIC_WO_CHAIN node used for if conversion.
// Right now the semantics are the same as SELECT's
SDValue
TMS320C64XLowering::LowerIfConv(SDValue op, SelectionDAG &DAG) const
{
	MachineFunction &MF = DAG.getMachineFunction();
	MachineRegisterInfo &RegInfo = MF.getRegInfo();
  DebugLoc dl = op.getDebugLoc();
  unsigned IntNo = cast<ConstantSDNode>(op.getOperand(0))->getZExtValue();
  assert (IntNo == Intrinsic::vliw_ifconv_t);

	// Operand 1 is true/false, selects operand 2 or 3 respectively
	// We'll generate this with two conditional move instructions - moving
	// the true/false result to the same register. In theory these could
	// be scheduled in the same insn packet (given that only one will
	// execute out of the pair, due to the conditional)

	SDValue ops[4];
	ops[0] = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i32, op.getOperand(2)); // true val
	ops[1] = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i32, op.getOperand(3)); // false val
	ops[2] = DAG.getTargetConstant(0, MVT::i32); // always 0 ?
	ops[3] = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i32, op.getOperand(1)); // condition
  SDValue Chain = DAG.getNode(TMSISD::SELECT, dl, MVT::i32, ops, 4);

  // make sure the same we end up with the same type as the instrinsic (ie. i32
  // by default or i1 through truncation)
  if (op.getValueType() == MVT::i1)
    Chain = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, Chain);
  return Chain;
}

// Lower the predication intrinsic to a pseudo node
SDValue
TMS320C64XLowering::LowerIntrinsicVoid(SDValue op, SelectionDAG &DAG) const
{
	MachineFunction &MF = DAG.getMachineFunction();
  DebugLoc dl = op.getDebugLoc();
  unsigned IntNo = cast<ConstantSDNode>(op.getOperand(1))->getZExtValue();
  assert (IntNo == Intrinsic::vliw_predicate_mem);

  MemSDNode *M = dyn_cast<MemSDNode>(op.getOperand(0));
  assert(M);

	SDValue ops[3];
	ops[0] = op.getOperand(0); // chain
	ops[1] = PromoteBoolean(op.getOperand(2), DAG); // predicate true/false bit
	ops[2] = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i32, op.getOperand(3)); // condition
  return DAG.getNode(TMSISD::PRED_STORE, dl, MVT::Other, ops, 3);
}

void TMS320C64XLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue>&Results,
                                             SelectionDAG &DAG) {
  // Catch all intrinsics here that we want to custom lower, but not any other
  // nodes that LLVM promotes/expands/etc.
  switch (N->getOpcode()) {
    default: llvm_unreachable("only for intrinsics!"); return;

    case ISD::INTRINSIC_WO_CHAIN:
    case ISD::INTRINSIC_VOID:
      break;
  }

  SDValue Res = LowerOperation(SDValue(N, 0), DAG);
  if (Res.getNode())
    Results.push_back(Res);
}

// XXX: this is a hack, remove when intrinsic lowering is fixed.
// we would actually expect all bools to be promoted to i32 after legalization
SDValue TMS320C64XLowering::PromoteBoolean(SDValue op, SelectionDAG &DAG) {
  if (op->getValueType(0) != MVT::i1)
    return op;

  ConstantSDNode *Const = dyn_cast<ConstantSDNode>(op);
  if (Const)
    return DAG.getTargetConstant(Const->getSExtValue() ? 1 : 0, MVT::i32);

  if (op->getOpcode() == ISD::TRUNCATE) {
    SDValue TruncParent = op->getOperand(0);
    assert(TruncParent.getValueType() == MVT::i32);
    return TruncParent;
  }

  assert(false && "unknown promotion");
  return op;
}
