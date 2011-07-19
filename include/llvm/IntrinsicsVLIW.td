let TargetPrefix = "vliw" in {  // All intrinsics start with "llvm.vliw.".
  def int_vliw_ifconv_t :
    Intrinsic<[llvm_anyint_ty],
              [llvm_i1_ty, LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  def int_vliw_predicate_mem :
    Intrinsic<[], [llvm_i1_ty, llvm_i1_ty, llvm_anyptr_ty], []>;
}
