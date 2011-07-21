//===-- llvm/CodeGen/MachineSuperBlock.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions/basic blocks for a "superblock".
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "machine-superblock"
#include "llvm/CodeGen/MachineSuperBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//-----------------------------------------------------------------------------

MachineSuperBlock::MachineSuperBlock(const MBBListTy &MBBs, unsigned count)
: MBBListTy(MBBs),
  execCount(count)
{
  // throw in the references to the machine instructions now, thats not very
  // efficient, but well, it works for now, and may probably be improved later
  for (iterator I = begin(); I != end(); ++I) {
    for (MachineBasicBlock::iterator J = (*I)->begin(); J != (*I)->end(); ++J)
      instrList.push_back(&*J);

    if ((*I)->succ_size() > 1) sideExits.push_back(*I);
  }
}

//-----------------------------------------------------------------------------

MachineSuperBlock::~MachineSuperBlock() {}

//-----------------------------------------------------------------------------

void MachineSuperBlock::verify() const {
  if (!size()) return;

  for (const_iterator I = begin(); I != end(); ++I) {
    assert(*I && "Invalid MBB within a superblock!");

    if (I == begin()) continue;

    // verify side entries. The header bb may have any number of side-exits,
    // all other blocks included in the superblock are checked to have only
    // 1 pred (preceeding in the superblock)
    if ((*I)->pred_size() > 1) {
      dbgs() << "ERROR: Bad entry into a superblock detected!\n";
      dbgs() << "MBB " << (*I)->getName() << " has " << (*I)->pred_size() - 1;
      dbgs() << " side entries. Superblock constraints violated!\n";
    }
  }
}

//-----------------------------------------------------------------------------

void MachineSuperBlock::print(const bool showInstructions) const {
  dbgs() << "Superblock [exec count: " << execCount << "]\n";
  for (const_iterator I = begin(); I != end(); ++I) {
    assert(*I && "Invalid MBB within a superblock!");
    if (showInstructions) (*I)->dump();
    else dbgs() << "  " << (*I)->getName() << '\n';
  }
}

//-----------------------------------------------------------------------------

bool MachineSuperBlock::isRegUsed(unsigned reg) const {
  const MachineRegisterInfo &MRI = front()->getParent()->getRegInfo();

  MachineRegisterInfo::use_iterator UI;
  for (UI = MRI.use_begin(reg); UI != MRI.use_end(); ++UI)
    if (contains(*(*UI).getParent())) return true;
  return false;
}

//-----------------------------------------------------------------------------

bool MachineSuperBlock::isRegUsedOnly(unsigned reg) const {
  const MachineRegisterInfo &MRI = front()->getParent()->getRegInfo();

  MachineRegisterInfo::use_iterator UI;
  for (UI = MRI.use_begin(reg); UI != MRI.use_end(); ++UI)
    if (!contains(*(*UI).getParent())) return false;
  return true;
}

//-----------------------------------------------------------------------------

std::set<unsigned> MachineSuperBlock::getLiveOutRegDefs() const {
  std::set<unsigned> regSet;

  for (const_instr_iterator I = instrList.begin(); I != instrList.end(); ++I) {
    for (unsigned N = 0; N < (*I)->getNumOperands(); ++N) {
      MachineOperand &MO = (*I)->getOperand(N);
      if (!(MO.isDef() && MO.isReg())) continue;
      unsigned reg = MO.getReg();
      if (!isRegUsedOnly(reg)) regSet.insert(reg);
    }
  }
  return regSet;
}


