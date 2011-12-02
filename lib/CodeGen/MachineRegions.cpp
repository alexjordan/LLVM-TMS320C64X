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

#define DEBUG_TYPE "machine-regions"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>

using namespace llvm;

//-----------------------------------------------------------------------------

MachineSingleEntryRegion::MachineSingleEntryRegion(const MachineFunction &F,
                                                   MachineBasicBlock &E,
                                                   const MBBListTy &BBs)
: parent(&F),
  entry(&E),
  blocks(BBs)
{}

//-----------------------------------------------------------------------------

MachineSingleEntryRegion::MachineSingleEntryRegion(const MachineFunction &F,
                                                   MachineBasicBlock &MBB)
: parent(&F),
  entry(&MBB)
{
  blocks.push_back(&MBB);
}


//-----------------------------------------------------------------------------

bool MachineSingleEntryRegion::contains(MachineBasicBlock *MBB) const {
  assert(MBB && "Invalid machine basic block specified!");
  return std::find(blocks.begin(), blocks.end(), MBB) != blocks.end();
}

//-----------------------------------------------------------------------------

bool MachineSingleEntryRegion::isExit(MachineBasicBlock *MBB) const {
  // must not be a part of a region
  if (contains(MBB)) return false;
  MachineBasicBlock::pred_iterator PI;
  for (PI = MBB->pred_begin(); PI != MBB->pred_end(); ++PI) {
    // if any of the predecessors is contained in the
    // region, then we have an exit machine basic block
    if (contains(*PI)) return true;
  }
  return false;
}

//-----------------------------------------------------------------------------

bool MachineSingleEntryRegion::isExiting(MachineBasicBlock *MBB) const {
  // must be a part of a region
  if (!contains(MBB)) return false;
  MachineBasicBlock::succ_iterator SI;
  for (SI = MBB->succ_begin(); SI != MBB->succ_end(); ++SI) {
    // if any of the successors is outside of the region
    // we are obviously dealing with an exiting block
    if (!contains(*SI)) return true;
  }
  return false;
}

//-----------------------------------------------------------------------------

bool MachineSingleEntryRegion::hasRegUses(unsigned reg) const {
  assert(blocks.size() && "Region empty, can not analyze registers!");
  const MachineRegisterInfo &MRI = parent->getRegInfo();

  MachineRegisterInfo::use_iterator UI;
  for (UI = MRI.use_begin(reg); UI != MRI.use_end(); ++UI)
    if (contains((*UI).getParent())) return true;
  return false;
}

//-----------------------------------------------------------------------------

void MachineSingleEntryRegion::verify() {
  assert(parent && entry && "Bad parent/entry for a single-entry region!");
  assert(contains(entry) && "Entry not contained in the block list!");

  // verify the single-entry property, check all blocks and inspect their
  // predecessors which may enter the region from outside. If such a pred is
  // found, the single-entry property is violated
  for (MBBListTy::const_iterator I = blocks.begin(); I != blocks.end(); ++I) {
    assert(*I && "Bad block found within a region!");
    assert(!(*I)->isSuccessor(*I) && "Cycle detected within a region!");

    if (*I == entry) continue;

    // check the IR basic block corresponding to the machine basic block
    assert((*I)->getBasicBlock() && "Bad corresponding IR basic block!");

    MachineBasicBlock::pred_iterator PI;
    for (PI = (*I)->pred_begin(); PI != (*I)->pred_end(); ++PI)
      assert(contains(*PI) && "Multiple entries into a region found!");
  }
}

//-----------------------------------------------------------------------------

void MachineSingleEntryRegion::print(bool showInstructions) const {
  dbgs() << "Single-Entry region of machine basic blocks:\n";
  for (MBBListTy::const_iterator I = blocks.begin(); I != blocks.end(); ++I) {
    assert(*I && "Invalid MBB found within a region!");
    if (showInstructions) (*I)->dump();
    else dbgs() << "  " << (*I)->getName() << '\n';
  }
}

//-----------------------------------------------------------------------------
// MachineSingleEntryPathRegion
//-----------------------------------------------------------------------------

void MachineSingleEntryPathRegion::verify() {
  // run all checks provided by the parent first
  MachineSingleEntryRegion::verify();

  /// additionally provide some verification specific to paths, for that we
  /// require the list of basic blocks to be "ordered", i.e. beginning with
  /// the entry and continuing successor-wise

  assert(*begin() == entry && "Invalid entry for the sorted path region");

  MachineBasicBlock *prevMBB = entry;
  for (const_iterator I = begin(); I != end(); ++I) {
    // skip the entry basic block
    if ((*I) == entry) continue;

    // now verify the linked chain of machine basic blocks within the path
    assert(prevMBB->isSuccessor(*I) && "Bad linkage for a path region!");
    prevMBB = *I;
  }
}

//-----------------------------------------------------------------------------

void MachineSingleEntryPathRegion::sort() {
  if (size() <= 1) return;

  MBBListTy orderedList;
  MachineBasicBlock *MBB = entry;

  while (1) {
    orderedList.push_back(MBB);

    bool succFound = false;

    MachineBasicBlock::succ_iterator SI;
    for (SI = MBB->succ_begin(); SI != MBB->succ_end(); ++SI) {
      // for a path region, there can only be one successor of the current
      // block contained in the path region, therefore look for this one and
      // continue with it
      if (contains(*SI) && (*SI != entry)) {
        succFound = true;
        MBB = *SI;
        break;
      }
    }

    if (!succFound) break;
  }

  blocks = orderedList;
}

//-----------------------------------------------------------------------------
// MachineSingleEntryCFGRegion
//-----------------------------------------------------------------------------

void MachineSingleEntryCFGRegion::verify() {
  MachineSingleEntryRegion::verify();
}

//-----------------------------------------------------------------------------

void MachineSingleEntryCFGRegion::sort() {
  if (size() <= 1) return;

  assert(entry && "Invalid entry for the region!");

  // now implement the basic algorithm for the breadth-first-search, we use a
  // a queue (Q) to manage the successors, sorted result is then available in
  // a new region object (R)

  std::queue<MachineBasicBlock*> Q;
  MBBListTy R;

  Q.push(entry);
  R.push_back(entry);

  while (Q.size()) {
    MachineBasicBlock *MBB = Q.front();
    Q.pop();

    // now inspect any successors of the current block
    MachineBasicBlock::succ_iterator SI;
    for (SI = MBB->succ_begin(); SI != MBB->succ_end(); ++SI) {
      // check whether we have inspected current successor already
      MBBListTy::const_iterator J = find(R.begin(), R.end(), MBB);

      if (J != R.end()) {
        R.push_back(*SI);
        Q.push(*SI);
      }
    }
  }

  blocks = R;
}

//-----------------------------------------------------------------------------
// MachineSingleEntryRegion::InstrIterator
//-----------------------------------------------------------------------------
MachineSingleEntryRegion::InstrIterator::InstrIterator(
                                    MachineSingleEntryRegion *MR,
                                    MachineSingleEntryRegion::iterator I)
  : MRI(I),
    MRBegin(MR->begin()),
    MREnd(MR->end()) {

  assert(MRBegin != MREnd && "empty region");
  if (MRI != MREnd) {
    MBBI = curBlock().begin();
    assert(MBBI != curBlock().end() && "empty block in region");
  }
  // otherwise we are in end-state and leave MBBI at its default
}

MachineSingleEntryRegion::InstrIterator
MachineSingleEntryRegion::InstrIterator::operator++() {
  if (++MBBI == curBlock().end())
    nextBlock();
  return *this;
}

MachineSingleEntryRegion::InstrIterator
MachineSingleEntryRegion::InstrIterator::operator--() {
  if (atEnd() || MBBI == curBlock().begin())
    prevBlock();
  else
    --MBBI;
  return *this;
}

bool MachineSingleEntryRegion::InstrIterator::atEnd() const {
  if (MRI == MREnd) {
    assert(MBBI == MachineBasicBlock::iterator() && "incosistent end state");
    return true;
  }
  return false;
}

void MachineSingleEntryRegion::InstrIterator::nextBlock() {

  // advance to the next block in the list
  if (++MRI == MREnd) {
    // no blocks left, reset instr iterator to initial state
    MBBI = MachineBasicBlock::iterator();
  } else {
    // set instr iterator to begin of next block
    MBBI = curBlock().begin();
    assert((MBBI != curBlock().end()) && "unexpected empty MBB in region");
  }
}

void MachineSingleEntryRegion::InstrIterator::prevBlock() {

  // back to the previous block in the list
  if (MRI == MRBegin) {
    // no blocks left, reset instr iterator to initial state
    MBBI = MachineBasicBlock::iterator();
  } else {
    --MRI;
    // set instr iterator to begin of next block
    MBBI = curBlock().end();
    assert((MBBI != curBlock().begin()) && "unexpected empty MBB in region");
    --MBBI;
  }
}
