//==- TMS320C64XRegisterInfo.td - TMS320C64X Register File -*- tablegen -*-==//
// 
//                     The LLVM Compiler Infrastructure
//
// This file Copyright Jeremy Morse (jmorse+llvm@studentrobotics.org), pending
// what student robotics decides to do with this code
//
//===----------------------------------------------------------------------===//

class MainReg<bits<6> num, bit side, string n> : Register<n> {
	field bits<6> Num = num;
	bit side = side;
	let Namespace = "TMS320C64X";
}

def A0	:	MainReg<0, 0, "A0">, DwarfRegNum<[0]>;
def A1	:	MainReg<1, 0, "A1">, DwarfRegNum<[1]>;
def A2	:	MainReg<2, 0, "A2">, DwarfRegNum<[2]>;
def A3	:	MainReg<3, 0, "A3">, DwarfRegNum<[3]>;
def A4	:	MainReg<4, 0, "A4">, DwarfRegNum<[4]>;
def A5	:	MainReg<5, 0, "A5">, DwarfRegNum<[5]>;
def A6	:	MainReg<6, 0, "A6">, DwarfRegNum<[6]>;
def A7	:	MainReg<7, 0, "A7">, DwarfRegNum<[7]>;
def A8	:	MainReg<8, 0, "A8">, DwarfRegNum<[8]>;
def A9	:	MainReg<9, 0, "A9">, DwarfRegNum<[9]>;
def A10	:	MainReg<10, 0, "A10">, DwarfRegNum<[10]>;
def A11	:	MainReg<11, 0, "A11">, DwarfRegNum<[11]>;
def A12	:	MainReg<12, 0, "A12">, DwarfRegNum<[12]>;
def A13	:	MainReg<13, 0, "A13">, DwarfRegNum<[13]>;
def A14	:	MainReg<14, 0, "A14">, DwarfRegNum<[14]>;
def A15	:	MainReg<15, 0, "A15">, DwarfRegNum<[15]>;
def A16	:	MainReg<16, 0, "A16">, DwarfRegNum<[16]>;
def A17	:	MainReg<17, 0, "A17">, DwarfRegNum<[17]>;
def A18	:	MainReg<18, 0, "A18">, DwarfRegNum<[18]>;
def A19	:	MainReg<19, 0, "A19">, DwarfRegNum<[19]>;
def A20	:	MainReg<20, 0, "A20">, DwarfRegNum<[20]>;
def A21	:	MainReg<21, 0, "A21">, DwarfRegNum<[21]>;
def A22	:	MainReg<22, 0, "A22">, DwarfRegNum<[22]>;
def A23	:	MainReg<23, 0, "A23">, DwarfRegNum<[23]>;
def A24	:	MainReg<24, 0, "A24">, DwarfRegNum<[24]>;
def A25	:	MainReg<25, 0, "A25">, DwarfRegNum<[25]>;
def A26	:	MainReg<26, 0, "A26">, DwarfRegNum<[26]>;
def A27	:	MainReg<27, 0, "A27">, DwarfRegNum<[27]>;
def A28	:	MainReg<28, 0, "A28">, DwarfRegNum<[28]>;
def A29	:	MainReg<29, 0, "A29">, DwarfRegNum<[29]>;
def A30	:	MainReg<30, 0, "A30">, DwarfRegNum<[30]>;
def A31	:	MainReg<31, 0, "A31">, DwarfRegNum<[31]>;
def B0	:	MainReg<32, 1, "B0">, DwarfRegNum<[32]>;
def B1	:	MainReg<33, 1, "B1">, DwarfRegNum<[33]>;
def B2	:	MainReg<34, 1, "B2">, DwarfRegNum<[34]>;
def B3	:	MainReg<35, 1, "B3">, DwarfRegNum<[35]>;
def B4	:	MainReg<36, 1, "B4">, DwarfRegNum<[36]>;
def B5	:	MainReg<37, 1, "B5">, DwarfRegNum<[37]>;
def B6	:	MainReg<38, 1, "B6">, DwarfRegNum<[38]>;
def B7	:	MainReg<39, 1, "B7">, DwarfRegNum<[39]>;
def B8	:	MainReg<40, 1, "B8">, DwarfRegNum<[40]>;
def B9	:	MainReg<41, 1, "B9">, DwarfRegNum<[41]>;
def B10	:	MainReg<42, 1, "B10">, DwarfRegNum<[42]>;
def B11	:	MainReg<43, 1, "B11">, DwarfRegNum<[43]>;
def B12	:	MainReg<44, 1, "B12">, DwarfRegNum<[44]>;
def B13	:	MainReg<45, 1, "B13">, DwarfRegNum<[45]>;
def B14	:	MainReg<46, 1, "B14">, DwarfRegNum<[46]>;
def B15	:	MainReg<47, 1, "B15">, DwarfRegNum<[47]>;
def B16	:	MainReg<48, 1, "B16">, DwarfRegNum<[48]>;
def B17	:	MainReg<49, 1, "B17">, DwarfRegNum<[49]>;
def B18	:	MainReg<50, 1, "B18">, DwarfRegNum<[50]>;
def B19	:	MainReg<51, 1, "B19">, DwarfRegNum<[51]>;
def B20	:	MainReg<52, 1, "B20">, DwarfRegNum<[52]>;
def B21	:	MainReg<53, 1, "B21">, DwarfRegNum<[53]>;
def B22	:	MainReg<54, 1, "B22">, DwarfRegNum<[54]>;
def B23	:	MainReg<55, 1, "B23">, DwarfRegNum<[55]>;
def B24	:	MainReg<56, 1, "B24">, DwarfRegNum<[56]>;
def B25	:	MainReg<57, 1, "B25">, DwarfRegNum<[57]>;
def B26	:	MainReg<58, 1, "B26">, DwarfRegNum<[58]>;
def B27	:	MainReg<59, 1, "B27">, DwarfRegNum<[59]>;
def B28	:	MainReg<60, 1, "B28">, DwarfRegNum<[60]>;
def B29	:	MainReg<61, 1, "B29">, DwarfRegNum<[61]>;
def B30	:	MainReg<62, 1, "B30">, DwarfRegNum<[62]>;
def B31	:	MainReg<63, 1, "B31">, DwarfRegNum<[63]>;

// Predicate registers: things that can be used for conditional execution
// in instructions: A0-2, B0-2.
def PredRegs : RegisterClass<"TMS320C64X", [i32], 32,
	[ A0, A1, A2, B0, B1, B2 ]>
{
let MethodProtos = [{
	iterator allocation_order_begin(const MachineFunction &MF) const;
	iterator allocation_order_end(const MachineFunction &MF) const;
}];
let MethodBodies = [{
	PredRegsClass::iterator
	PredRegsClass::allocation_order_begin(const MachineFunction &MF)
									const {
		 return begin();
	}
	PredRegsClass::iterator
	PredRegsClass::allocation_order_end(const MachineFunction &MF)
									const {
		return end() - 3; // Don't allocate B predicate registers
	}
}];
}


// Normal, general purpose 32 bit registers
def GPRegs : RegisterClass<"TMS320C64X", [i32], 32,
	[
		// A10-A15, B10-B15 are nonvolatile, B15 SP, A15 FP.
		// B14 is a data page pointer used by TIs C compiler for
		// the near memory model; we'll treat as reserved
		A3, A4, A5, A6, A7, A8, A9,
		A16, A17, A18, A19, A20, A21, A22, A23,
		A24, A25, A26, A27, A28, A29, A30, A31,
		/* Sparc and others put volatile / reserved registers at the
		 * end to decrease likelyhood of being allocated */
		A0, A1, A2, //Predicate regs
		A10, A11, A12, A13, A14,

		// Until a reasonable instruction + register scheduler gets
		// generated that can cope with xpath requirements in insn
		// packets, limit register allocations to side A of the proc
		B0, B1, B2,
		B3, B4, B5, B6, B7, B8, B9,
		B10, B11, B12, B13,
		B16, B17, B18, B19, B20, B21, B22, B23,
		B24, B25, B26, B27, B28, B29, B30, B31,
		A15, B14, B15
	] >
{
let MethodProtos = [{
	iterator allocation_order_begin(const MachineFunction &MF) const;
	iterator allocation_order_end(const MachineFunction &MF) const;
}];
let MethodBodies = [{
	GPRegsClass::iterator
	GPRegsClass::allocation_order_begin(const MachineFunction &MF)
									const {
		 return begin();
	}
	GPRegsClass::iterator
	GPRegsClass::allocation_order_end(const MachineFunction &MF)
									const {
		// Don't allocate SP/FP/DP
		// Or in fact any of the B registers
		return end()-33;
	}
}];
}

// Same regiseters, but seperate into different sides
def ARegs : RegisterClass<"TMS320C64X", [i32], 32,
	[
		A3, A4, A5, A6, A7, A8, A9,
		A16, A17, A18, A19, A20, A21, A22, A23,
		A24, A25, A26, A27, A28, A29, A30, A31,
		A0, A1, A2,			// Predicate regs
		A10, A11, A12, A13, A14,	// Nonvolatile
		A15				// Reserved
	] >
{
let MethodProtos = [{
	iterator allocation_order_begin(const MachineFunction &MF) const;
	iterator allocation_order_end(const MachineFunction &MF) const;
}];
let MethodBodies = [{
	ARegsClass::iterator
	ARegsClass::allocation_order_begin(const MachineFunction &MF)
									const {
		 return begin();
	}
	ARegsClass::iterator
	ARegsClass::allocation_order_end(const MachineFunction &MF)
									const {
		return end()-1;
	}
}];
}

def BRegs : RegisterClass<"TMS320C64X", [i32], 32,
	[
		B3, B4, B5, B6, B7, B8,
		B16, B17, B18, B19, B20, B21, B22, B23,
		B24, B25, B26, B27, B28, B29, B30, B31,
		B0, B1, B2,			// Predicate regs
		B10, B11, B12, B13,		// Nonvolatilie regs
		B14, B15
	] >
{
let MethodProtos = [{
	iterator allocation_order_begin(const MachineFunction &MF) const;
	iterator allocation_order_end(const MachineFunction &MF) const;
}];
let MethodBodies = [{
	BRegsClass::iterator
	BRegsClass::allocation_order_begin(const MachineFunction &MF)
									const {
		 return begin();
	}
	BRegsClass::iterator
	BRegsClass::allocation_order_end(const MachineFunction &MF)
									const {
		return end()-2;
	}
}];
}

// Our general purpose registers, but in vector form
def VectorRegs : RegisterClass<"TMS320C64X", [v4i8,v2i16], 32,
	[
		/* See comments above */
		A3, A4, A5, A6, A7, A8, A9, B3, B4, B5, B6, B7, B8,
		A16, A17, A18, A19, A20, A21, A22, A23,
		A24, A25, A26, A27, A28, A29, A30, A31,
		B16, B17, B18, B19, B20, B21, B22, B23,
		B24, B25, B26, B27, B28, B29, B30, B31,
		A0, A1, A2, B0, B1, B2,
		A10, A11, A12, A13, A14,
		B10, B11, B12, B13,
		A15, B14, B15
	] >
{
let MethodProtos = [{
	iterator allocation_order_begin(const MachineFunction &MF) const;
	iterator allocation_order_end(const MachineFunction &MF) const;
}];
let MethodBodies = [{
	VectorRegsClass::iterator
	VectorRegsClass::allocation_order_begin(const MachineFunction &MF)
									const {
		 return begin();
	}
	VectorRegsClass::iterator
	VectorRegsClass::allocation_order_end(const MachineFunction &MF)
									const {
		/* See comment above */
		return end()-3;
	}
}];
}
