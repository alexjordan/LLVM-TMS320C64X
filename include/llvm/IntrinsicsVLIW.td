let TargetPrefix = "vliw" in {  // All intrinsics start with "llvm.vliw.".
  def int_vliw_ifconv_t :
    Intrinsic<[llvm_i32_ty], [llvm_i1_ty, llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;
}
