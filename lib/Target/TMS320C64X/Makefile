##===- lib/Target/TMS320C64X/Makefile ----------------------*- Makefile -*-===##

LEVEL = ../../..
LIBRARYNAME = LLVMTMS320C64XCodeGen
TARGET = TMS320C64X

# Make sure that tblgen is run, first thing.
BUILT_SOURCES =	TMS320C64XGenRegisterInfo.h.inc \
		TMS320C64XGenRegisterNames.inc \
		TMS320C64XGenRegisterInfo.inc \
		TMS320C64XGenInstrNames.inc \
		TMS320C64XGenInstrInfo.inc \
		TMS320C64XGenAsmWriter.inc \
		TMS320C64XGenCallingConv.inc \
		TMS320C64XGenDAGISel.inc \
    TMS320C64XGenSubtarget.inc

DIRS = TargetInfo

include $(LEVEL)/Makefile.common

