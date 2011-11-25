//===-- TMS320C64XHazardRecognizer.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Alexander Jordan, Vienna University of Technology,
// and is distributed under the University of Illinois Open Source License.
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares the hazard recognizer for the TMS320C64X.
//
//===----------------------------------------------------------------------===//

#ifndef TMS320C64XHAZARDRECOGNIZER_H
#define TMS320C64XHAZARDRECOGNIZER_H

#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/Target/TargetRegisterInfo.h"

#include <string>
#include <set>

namespace llvm {

class TargetInstrInfo;
class TargetInstrDesc;
class MachineInstr;
class MachineOperand;

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

/// Tracks resource use and schedules functional units
class ResourceAssignment {
protected:
  const TargetInstrInfo &TII;
  TMS320C64X::MachineHazards *Hzd;

  void Reset();
  bool isPseudo(SUnit *SU) const;
  unsigned getUnitIndex(unsigned side, unsigned unit);
  unsigned getUnitIndex(SUnit *SU);
public:
  ResourceAssignment(const TargetInstrInfo &TII);
  virtual ~ResourceAssignment();

  /// Try to schedule SU in the current cycle, return true if successful.
  /// Note: May change FU operand.
  bool Schedule(SUnit *SU);

  /// Same as schedule, but try to schedule SU on the given cluster
  /// (ie. ignore the side encoded in the instruction).
  bool ScheduleOnSide(SUnit *SU, unsigned side);

  bool ScheduleXCC(unsigned dstSide);
  bool XPathOnSideAvailable(unsigned dstSide);

  /// End the current cycle.
  void AdvanceCycle();

  void dbgUnitBusy(SUnit *SU, unsigned idx) const;
  void dbgExtraBusy(SUnit *SU, unsigned xidx) const;

  static bool isFlexibleInstruction(const TargetInstrDesc &tid);
  static void setXPath(MachineInstr *MI, bool setOrUnset);
  static unsigned getExtraUse(SUnit *SU);
  static unsigned getExtraUse(const TargetInstrDesc &desc,
                              const MachineOperand &op);
  static std::pair<bool, int> analyzeOpRegs(const MachineInstr *MI);
  static std::set<const TargetRegisterClass*>
    getOperandRCs(const MachineInstr *MI);
  static std::string getExtraStr(unsigned xuse);

private:
  bool isCopy(SUnit *SU) const;
};

} // end of namespace TMS320C64X


/// Implements hazard recognizer interface, does not schedule FUs.
class TMS320C64XHazardRecognizer : public ScheduleHazardRecognizer,
                                   public TMS320C64X::ResourceAssignment {

  bool isMove(SUnit *SU) const;
  void fixResources(SUnit *SU);
  void activateExtra(SUnit *SU);
  HazardType getMoveHazard(SUnit *SU) const;
  bool emitMove(SUnit *SU);

public:
  static std::pair<unsigned,unsigned> analyzeMove(SUnit *SU);

  TMS320C64XHazardRecognizer(const TargetInstrInfo &TII)
    : ResourceAssignment(TII)
  {}

  virtual HazardType getHazardType(SUnit *SU, int stalls);
  virtual void EmitInstruction(SUnit *SU);
  virtual void EmitNoop();
  virtual void AdvanceCycle() { ResourceAssignment::AdvanceCycle(); }
  virtual void Reset() { ResourceAssignment::Reset(); }
};

} // end namespace llvm

#endif
