//==- TMS320C64XSchedule.td - TMS320C64X Scheduler spec ----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Scheduler specification for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Functional units (always one per cluster)

def L1 : FuncUnit;
def L2 : FuncUnit;

def S1 : FuncUnit;
def S2 : FuncUnit;

def M1 : FuncUnit;
def M2 : FuncUnit;

def D1 : FuncUnit;
def D2 : FuncUnit;

//===----------------------------------------------------------------------===//
// Instruction Itinerary classes

def Default    : InstrItinClass;
def Load       : InstrItinClass;
def Store      : InstrItinClass;
def Multiply   : InstrItinClass;
def Multiply16 : InstrItinClass;
def Branch     : InstrItinClass;

//===----------------------------------------------------------------------===//
// Instruction Itinerary classes

def TMS320X64XItineraries : ProcessorItineraries<
  [L1, L2, S1, S2, M1, M2, D1, D2],
  [],
  [ InstrItinData<Default, [InstrStage<1, [L1, L2]>]>,  // 0 delay slots
    InstrItinData<Load, [InstrStage<5, [D1, D2]>]>,     // 4 delay slots
    InstrItinData<Store, [InstrStage<1, [D1, D2]>]>,    // 0 delay slots
    InstrItinData<Multiply, [InstrStage<4, [M1, M2]>]>, // 3 delay slots
    InstrItinData<Multiply16, [InstrStage<2, [M1, M2]>]>, // 1 delay slot
    InstrItinData<Branch, [InstrStage<6, [S1, S2]>]>    // 5 delay slots
  ]
>;
