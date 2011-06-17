//===-- TMS320C64XGenCallingConv.td - TMS320C64X Calling convention  --===//
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

// NKIM, hard-code here if desired, currently, the custom calling
// convention is implemented within the TargetLowering unit and
// hooked up from the default CC_TMS320C64 calling convention by
// name
// def CC_TMS320C64X_Custom : CallingConv<[]>;

def RetCC_TMS320C64X : CallingConv<[
        // XXX - returning structs is a black arg
        CCIfType<[i8,i16], CCPromoteToType<i32>>,
        // XXX - for 64 bit returns I assume we use the A4/A5 pair.. uh..
        // need to actually check this in spec though
        CCIfType<[i32, f32], CCAssignToReg<[A4,A5]>>,
        CCIfType<[i64, f64], CCAssignToRegWithShadow<[A4], [A5]>>
]>;

def CC_TMS320C64X : CallingConv<[
  // promote ints
  CCIfType<[i8,i16], CCPromoteToType<i32>>,
  // force 64-bit args into adjacent register pairs
  CCIfType<[i32,f32], CCCustom<"CC_TMS320C64X_Custom">>,

  // XXX stuff below would only work if we had a proper i64 type
        //CCIfType<[i64],
  //  CCAssignToRegWithShadow<[A4,B4,A6,B6,A8,B8,A10,B10,A12,B12],
  //                          [A5,B5,A7,B7,A9,B9,A11,B11,A13,B13]>>,

  CCAssignToStack<0, 0>
]>;

