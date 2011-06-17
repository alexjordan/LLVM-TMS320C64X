//===-- llvm/ ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TMS320C64XHAZARDRECOGNIZER_H
#define TMS320C64XHAZARDRECOGNIZER_H

#include "llvm/CodeGen/ScheduleHazardRecognizer.h"

#include <string>

namespace llvm {

class TargetInstrInfo;
class MachineInstr;

namespace TMS320C64X {

class MachineHazards;

enum ExtraRes {
  None = 0,
  T1 = 1,
  T2 = 2,
  X1 = 3,
  X2 = 4,
  NumExtra = 5
};

} // end of namespace TMS320C64X

/// TMS320C64XHazardRecognizer

class TMS320C64XHazardRecognizer : public ScheduleHazardRecognizer {

  const TargetInstrInfo &TII;
  TMS320C64X::MachineHazards *Hzd;

  unsigned getUnitIndex(unsigned side, unsigned unit);
  unsigned getUnitIndex(SUnit *SU);
  bool isPseudo(SUnit *SU) const;
  bool isMove(SUnit *SU) const;
  void fixResources(SUnit *SU);
  void setXPath(MachineInstr *MI, bool setOrUnset) const;
  void activateExtra(SUnit *SU);
  HazardType getMoveHazard(SUnit *SU) const;
  bool emitMove(SUnit *SU);

public:
  static unsigned getExtraUse(SUnit *SU);
  static std::pair<unsigned,unsigned> analyzeMove(SUnit *SU);
  static std::string getExtraStr(unsigned xuse);
  static std::pair<bool, int> analyzeOpRegs(const MachineInstr *MI);

  TMS320C64XHazardRecognizer(const TargetInstrInfo &TII);
  ~TMS320C64XHazardRecognizer();

  virtual HazardType getHazardType(SUnit *SU, int stalls);

  virtual void EmitInstruction(SUnit *SU);
  virtual void AdvanceCycle();
  virtual void EmitNoop();
  virtual void Reset();
};

} // end namespace llvm

#endif
