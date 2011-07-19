//===-- llvm/CodeGen/MachineSuperBlock.h ------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_MACHINESUPERBLOCK_H
#define LLVM_CODEGEN_MACHINESUPERBLOCK_H

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include <cassert>
#include <vector>
#include <list>
#include <algorithm>

namespace llvm {

//----------------------------------------------------------------------------

// temporary only, will probably be removed later
typedef std::list<MachineBasicBlock*> MBBListTy;
typedef std::vector<MachineBasicBlock*> MBBVectorTy;
typedef std::vector<MachineInstr*> MIVectorTy;

/// iterator types for storing/accessing machine instructions
typedef MIVectorTy::iterator instr_iterator;
typedef MIVectorTy::const_iterator const_instr_iterator;

// iterator types for storing/accessing machine basic blocks
typedef MBBVectorTy::iterator bb_iterator;
typedef MBBVectorTy::const_iterator const_bb_iterator;

//----------------------------------------------------------------------------

class MachineSuperBlock {

  /// Data containers. For the time being we provide only two containers for
  /// storing machine instructions and machine basic blocks. While redundant,
  /// the way of storing pointers to machine instructions in a single conse-
  /// quitive list may present a much more comfortable analysis way to users
  MBBVectorTy mbbVector;
  MIVectorTy  miVector;

  /// this reflects the execution frequency (as obtained from the profiling
  /// path information) of the entire superblock and may serve for estimate
  /// the weight/priority of the superblock (f.e. during scheduling)
  unsigned execCount;

public:

  /// Ctors/dtors
  MachineSuperBlock(const MBBVectorTy &MBBs, unsigned count);
  MachineSuperBlock(const MBBListTy &MBBs, unsigned count);
  ~MachineSuperBlock() { /* empty */ }

  /// Iterators for machine instructions
  instr_iterator mi_begin() { return miVector.begin(); }
  instr_iterator mi_end() { return miVector.end(); }

  const_instr_iterator mi_begin() const { return miVector.begin(); }
  const_instr_iterator mi_end() const { return miVector.end(); }

  /// Iterators for machine basic blocks
  bb_iterator bb_begin() { return mbbVector.begin(); }
  bb_iterator bb_end() { return mbbVector.end(); }

  const_bb_iterator bb_begin() const { return mbbVector.begin(); }
  const_bb_iterator bb_end() const { return mbbVector.end(); }

  /// Simple element accessors for instructions and blocks
  const MachineInstr &getMI(unsigned I) const {
    assert(I < miVector.size() && "Bad MI vector index!");
    return *(miVector[I]);
  }

  const MachineBasicBlock &getMBB(unsigned I) const {
    assert(I < mbbVector.size() && "Bad MBB vector index!");
    return *(mbbVector[I]);
  }

  /// Simple accessors to obtain the entire container at once
  const MIVectorTy &getMachineInstructions() const { return miVector; }
  const MBBVectorTy &getMachineBlocks() const { return mbbVector; }

  /// Misc. simple accessors for convenience of usage only
  bool hasMachineInstructions() const { return miVector.size(); }
  bool hasMachineBlocks() const { return mbbVector.size(); }
  unsigned getExecCount() const { return execCount; }

  /// searching, look-up functionality
  MachineInstr *find(MachineInstr &MI) const {
    const_instr_iterator I =
      std::find(miVector.begin(), miVector.end(), &MI);
    return I == miVector.end() ? 0 : *I;
  }

  MachineBasicBlock *find(MachineBasicBlock &MBB) const {
    const_bb_iterator I =
      std::find(mbbVector.begin(), mbbVector.end(), &MBB);
    return I == mbbVector.end() ? 0 : *I;
  }

  bool contains(MachineBasicBlock &MBB) const { return find(MBB); }
  bool contains(MachineInstr &MI) const { return find(MI); }

  /// More sophisticated methods/accessors for working with superblocks will
  /// come soon, to simplify handling and to provide some basic anaylisis to
  /// all the users of the superblock-info


  /// this method is convenient for debugging when it comes to verify super-
  /// block-properties (such as successors/predeccessors/side-entries, etc.)
  void verify() const;

  /// this method may also be convenient for debugging to emit the content
  /// of the superblocks. If desired, the entire content of basic blocks will
  /// be dumped. If not, only the names of the machine basic blocks will be
  /// printed
  void print(bool showInstructions = false) const;
};

} // namespace llvm

#endif
