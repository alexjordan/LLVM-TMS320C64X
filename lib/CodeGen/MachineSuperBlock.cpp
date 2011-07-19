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
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//-----------------------------------------------------------------------------

MachineSuperBlock::MachineSuperBlock(const MBBVectorTy &MBBs, unsigned count)
: mbbVector(MBBs),
  execCount(count)
{
  // TODO create references to the machine instructions
}

//-----------------------------------------------------------------------------

MachineSuperBlock::MachineSuperBlock(const MBBListTy &MBBs, unsigned count)
: mbbVector(MBBs.size(), 0),
  execCount(count)
{
  unsigned J = 0;

  // throw in the references to the machine basic blocks first, thats not very
  // efficient, but well, it works for now, and may probably be improved later
  for (MBBListTy::const_iterator I = MBBs.begin(); I != MBBs.end(); ++I, ++J)
    mbbVector[J] = *I;

  // TODO, throw in instructions as well !
}

//-----------------------------------------------------------------------------

void MachineSuperBlock::verify() const {
  if (!mbbVector.size()) return;

  for (unsigned I = 0; I < mbbVector.size(); ++I) {

    assert(mbbVector[I] && "Invalid MBB within a superblock!");
    if (I == 0) continue;

    MachineBasicBlock *MBB = mbbVector[I];

    // verify "no-side-entry" property (except header)
    if (MBB->pred_size() != 1) {
      dbgs() << "ERROR: Bad entry into a superblock detected!\n";
      dbgs() << "MBB " << MBB->getName() << " has " << MBB->pred_size() - 1;
      dbgs() << " side entries. Superblock constraints violated!\n";
    }
  }
}

//-----------------------------------------------------------------------------

void MachineSuperBlock::print(const bool showInstructions) const {
  dbgs() << "Superblock [exec count: " << execCount << "]\n";
  for (unsigned I = 0; I < mbbVector.size(); ++I) {
    MachineBasicBlock *MBB = mbbVector[I];
    assert(MBB && "Invalid MBB within a superblock!");

    if (showInstructions) MBB->dump();
    else dbgs() << "  " << MBB->getName() << '\n';
  }
}
