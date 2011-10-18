CLANGPATH=/localtmp/ajordan/srobo/clang-llvm/Debug/bin
CLANGOPTS=-ccc-host-triple tms320c64x-unknown-gnu-linux -S -emit-llvm -o - \
		 -DSTART_PROFILING="asm(\"bench_begin:\")" \
		 -DEND_PROFILING="asm(\"bench_end:\")" \
		 -Dnear= -Dfar= \
		 -Xclang -isystem/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/include \
		 -Werror
OPTPATH=/localtmp/ajordan/srobo/llvm-tic6x/Debug/bin
OPTOPTS=-mem2reg -O2
LLC=/localtmp/ajordan/build/llvm29-merge/Debug+Asserts/bin/llc
LLCOPTS=-march=tms320c64x

GREP=grep -v "\.\(file\|type\)"
ASMHEAD="\t.compiler_opts --abi=eabi --c64p_l1d_workaround=default --endian=little --hll_source=on --silicon_version=6500"

AWKRET=awk 'BEGIN{ RS="retval = ";} { gsub(/[^0-9].*/,"",$$1); if ($$1 != ""){ print $$1; } }'

TICC=/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/bin/cl6x
TICCASMOPTS=-mv64+ -as -k --symdebug:none --abi=eabi
TICCOPTS= ${TICCASMOPTS} \
	   -i"/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/lib" \
	   -i"/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/include" \
     -i"../../include"

TIAR=/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/bin/ar6x

LDOPTS=-mv64+ \
	   --warn_sections -i"/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/lib" \
	   -i"/nfstmp/epicopt/opt/ccsv5/TI_CGT_C6000_7.0.3/include"  --reread_libs --rom_model

LOADTI=/localtmp/ajordan/ccs5_install/ccsv5/ccs_base_5.0.0.00053/scripting/examples/loadti/loadti.sh
CONFIG=/nfstmp/epicopt/opt/ccsv5/TargetConf/c64xAccurate.ccxml

define clang-cmd
${CLANGPATH}/clang ${CLANGOPTS} $< | ${OPTPATH}/opt ${OPTOPTS} -S -o $@
endef

define as-cmd
${CLANGPATH}/llvm-as $< -o $@
endef

define llc-cmd
${LLC} ${LLCOPTS} $< -o $@
endef

define ticc-asm-cmd
${TICC} ${TICCASMOPTS} -c $< --output_file=$@
endef

define ticc-compile-cmd
${TICC} ${TICCOPTS} -c $< --output_file=$@
endef

define link-cmd
${TICC} -z ${LDOPTS} -o $@ $^ -llibc.a lnk.cmd
endef

define clean-cmd
rm -f *.obj *.ll *.asm
endef

define bench-cmd
../benchmark.sh $<
endef

define run-cmd
${LOADTI} -c ${CONFIG} $<
endef
