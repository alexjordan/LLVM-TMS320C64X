//===- IntrinsicsTMS320C64X.td - Defines c64x intrinsics----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines TMS320X64X specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "c64x" in { // All intrinsics start with "llvm.c64x.".

  // start and return timestamp counter
  def int_c64x_timestamp_start : Intrinsic<[llvm_i32_ty],   [], []>;
  def int_c64x_timestamp_end : Intrinsic<[llvm_i32_ty],   [], []>;
}

