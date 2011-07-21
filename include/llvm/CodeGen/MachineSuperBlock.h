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
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/StringMap.h"
#include <cassert>
#include <list>
#include <set>
#include <algorithm>

namespace llvm {

//----------------------------------------------------------------------------

typedef std::list<MachineBasicBlock*> MBBListTy;
typedef std::list<MachineInstr*> MIListTy;

typedef MIListTy::iterator instr_iterator;
typedef MIListTy::const_iterator const_instr_iterator;

typedef MBBListTy::iterator exit_iterator;
typedef MBBListTy::const_iterator const_exit_iterator;

//----------------------------------------------------------------------------

class MachineSuperBlock : public std::list<MachineBasicBlock*> {

  /// Data container for machine instructions. This is an additional storage
  /// requirement (to store references to all machine instructions within a
  /// chain of basic blocks), but may be convenient for analysis purposes
  MIListTy instrList;

  /// a collection of side-exit basic blocks
  MBBListTy sideExits;

  /// the execution frequency of the superblock
  unsigned execCount;

public:

  // Ctors/dtors
  MachineSuperBlock(const MBBListTy &blocks, unsigned C = 0);
  ~MachineSuperBlock();

  /// Iterators for machine instructions
  instr_iterator mi_begin() { return instrList.begin(); }
  instr_iterator mi_end() { return instrList.end(); }

  const_instr_iterator mi_begin() const { return instrList.begin(); }
  const_instr_iterator mi_end() const { return instrList.end(); }

  /// Iterators for side-exit basic blocks
  exit_iterator se_begin() { return sideExits.begin(); }
  exit_iterator se_end() { return sideExits.end(); }

  const_exit_iterator se_begin() const { return sideExits.begin(); }
  const_exit_iterator se_end() const { return sideExits.end(); }

  /// Simple accessors to obtain entire containers at once
  const MIListTy &getInstrList() const { return instrList; }
  const MBBListTy &getSideExits() const { return sideExits; }

  /// Misc. simple accessors for convenience of usage only
  bool hasMachineInstructions() const { return instrList.size(); }
  bool hasSideExits() const { return sideExits.size(); }
  unsigned getExecCount() const { return execCount; }

  /// searching, look-up functionality. For the time being we provide bool
  /// look up functions only to check whether the element of the interest
  /// is contained in the superblock (instruction/side-exit/basic block)
  //
  bool contains(const MachineInstr &MI) const {
    const_instr_iterator I = std::find(mi_begin(), mi_end(), &MI);
    return I != instrList.end();
  }

  bool contains(const MachineBasicBlock &MBB) const {
    const_iterator I = std::find(begin(), end(), &MBB);
    return I != end();
  }

  bool isSideExit(const MachineBasicBlock &MBB) const {
    const_exit_iterator I = std::find(se_begin(), se_end(), &MBB);
    return I != sideExits.end();
  }

  /// check whether the specified (virtual) register does have uses in this
  /// superblock. This can be used for superblock-code motion analysis types
  bool isRegUsed(unsigned reg) const;

  /// check whether the specified (virtual) register is used exclusively in
  /// this superblock only, i.e. is not liveOut of the superblock
  bool isRegUsedOnly(unsigned reg) const;

  /// this method is expensive, will be rewritten evetually to return a set
  /// by arguments. It checks all register definitions within the superblock
  /// and returns a subset of them that are live-out of the superblock
  std::set<unsigned> getLiveOutRegDefs() const;
  
  /// TODO, liveIn, liveOut sets !

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
