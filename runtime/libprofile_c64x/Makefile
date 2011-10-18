include defs.mk

cfiles := $(wildcard *.c)

objfiles := $(cfiles:.c=.obj)

.PRECIOUS: %.bc %.ll %.asm

compile: libprofile.a

# compile for reference and move the .asm file that gets created in the cwd
%.obj: %.c
	$(ticc-compile-cmd)

%.out: %.obj
	$(link-cmd)

libprofile.a: $(objfiles)
	${TIAR} r $@ $^


clean:
	rm -f *.a *.bc *.s-gcc *.s-llvm gcc.out llvm.out *.bz2 *.asm *.obj *.out
	rm -f BreakpointsTestLog.xml BreakpointsTestLog.xml.*

