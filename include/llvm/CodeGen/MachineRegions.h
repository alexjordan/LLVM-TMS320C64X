//===-- llvm/CodeGen/MachineRegions.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declaration of classes for basic regions of machine basic blocks. 
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEREGIONS_H
#define LLVM_CODEGEN_MACHINEREGIONS_H

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/StringMap.h"
#include <cassert>
#include <list>
#include <set>
#include <algorithm>
#include <deque>

namespace llvm {

//----------------------------------------------------------------------------

typedef std::list<MachineBasicBlock*> MBBListTy;
typedef std::list<MachineInstr*> MIListTy;

typedef MBBListTy::iterator exit_iterator;
typedef MBBListTy::const_iterator const_exit_iterator;

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

class MachineSingleEntryRegion {

  public:

    // types for regular/const iterators
    typedef MBBListTy::iterator iterator;
    typedef MBBListTy::const_iterator const_iterator;

    // types for reverse/const reverse iterators
    typedef MBBListTy::reverse_iterator reverse_iterator;
    typedef MBBListTy::const_reverse_iterator const_reverse_iterator;

  protected:

    const MachineFunction *parent;
    MachineBasicBlock *entry;

    /// for the time being, we maintain all basic blocks which belong to a
    /// region within a simple list. This allows us to enforce an "order" of
    /// basic blocks, however, basic look up operations are expensive...
    MBBListTy blocks;

  public:

    /// region-ctor, for the time being fully parameterized for simplicity,
    /// note, that the passed information needs to be consistent. To assure
    /// the basic region properties, the verify method can be called after
    /// the construction
    MachineSingleEntryRegion(const MachineFunction &parentParam,
                             MachineBasicBlock &entryParam,
                             const MBBListTy &blocksParam);

    /// c-tor for a trivial region (consisting of one MBB)
    MachineSingleEntryRegion(const MachineFunction &parentParam,
                             MachineBasicBlock &entryParam);

    const MachineFunction *getParent() const { return parent; }
    const MachineBasicBlock *getEntry() const { return entry; }

    /// "begin" iterators for convenience. We do not yet export the entire
    /// list of basic blocks for a region in order to restrict the access to
    /// it and avoid inconsistencies
    iterator begin() { return blocks.begin(); }
    const_iterator begin() const { return blocks.begin(); }

    /// "end" iterators for convenience. We do not yet export the entire
    /// list of basic blocks for a region in order to restrict the access to
    /// it and avoid inconsistencies
    iterator end() { return blocks.end(); }
    const_iterator end() const { return blocks.end(); }

    /// also for convenience of iteration we provide reverse iterators here
    reverse_iterator rbegin() { return blocks.rbegin(); }
    const_reverse_iterator rbegin() const { return blocks.rbegin(); }

    reverse_iterator rend() { return blocks.rend(); }
    const_reverse_iterator rend() const { return blocks.rend(); }

    unsigned size() const { return blocks.size(); }

    /// a simple helper method to determine whether the specified machine bb
    /// is contained in the region
    bool contains(MachineBasicBlock *MBB) const;

    /// since we are dealing with regions of machine basic blocks which have
    /// only a single entry but may have a various number of side-exits, we
    /// provide a query to check for exit blocks. An exit block is a basic
    /// block outside of the region that does have a predecessor within the
    /// region
    bool isExit(MachineBasicBlock *MBB) const;

    /// since we are dealing with regions of machine basic blocks which have
    /// only a single entry but may have a various number of side-exits, we
    /// provide a query to check for exiting blocks. An exiting block is a
    /// block (which is a part of the region) which does branch to a basic
    /// block outside of the region
    bool isExiting(MachineBasicBlock *MBB) const;

    /// check whether the specified (virtual) register does have any uses in
    /// this region. This is handy for various region based optimizations/
    /// transformations
    bool hasRegUses(unsigned reg) const;

    /// this method is convenient for debugging when it comes to verify the
    /// region properties (i.e. especially check for cycles, side-entries,
    /// validity of the entry-block, etc)
    virtual void verify();

    /// this method may also be convenient for debugging to emit the content
    /// of the regions. If desired, the entire content of basic blocks will
    /// be dumped. If not, only the names of the machine basic blocks will be
    /// printed
    virtual void print(bool showInstructions = false) const;

    /// some applications require basic blocks of a region to be ordered in
    /// some manner. For paths (superblocks) it makes sense to order succes-
    /// sor-wise. For CFG-regions containing multiple paths a breadth-first
    /// ordering may be profitable
    virtual void sort() = 0;


    /// Iterator for instructions within a region.
    class InstrIterator
      : public std::iterator <std::bidirectional_iterator_tag, MachineInstr*,
                              std::ptrdiff_t, MachineInstr**, MachineInstr*> {
      MachineSingleEntryRegion::iterator MRI, MRBegin, MREnd;
      MachineBasicBlock::iterator MBBI;


      void nextBlock();
      void prevBlock();
      bool atEnd() const;
      MachineBasicBlock &curBlock() const { return **MRI; }
    public:
      /// constructs an iterator that points to begin of block I.
      /// If I == MR->end(), it is the end-iterator.
      explicit InstrIterator(MachineSingleEntryRegion *MR,
                             MachineSingleEntryRegion::iterator I);

      explicit InstrIterator() {}

      InstrIterator operator++();
      InstrIterator operator--();
      MachineInstr *operator*() { return MBBI; }

      friend
      bool operator==(InstrIterator a, InstrIterator b) {
        return (a.MRI == b.MRI &&
                a.MBBI == b.MBBI);
      }

      friend
      bool operator!=(InstrIterator a, InstrIterator b) { return !(a == b); }
    };

    typedef InstrIterator instr_iterator;
    typedef std::reverse_iterator<InstrIterator> instr_reverse_iterator;

    /// "begin" iterator for all instructions within the single entry region.
    /// The order of instructions iterated depends on the internal sorting of
    /// blocks within the concrete region.
    instr_iterator instr_begin() {
      return instr_iterator(this, blocks.begin()); }
    /// "end" iterator for all instructions within the single entry region.
    instr_iterator instr_end() {
      return instr_iterator(this, blocks.end()); }

    /// we also provide a reverse iterator for instructions.
    instr_reverse_iterator instr_rbegin() {
      return instr_reverse_iterator(instr_end());
    }
    instr_reverse_iterator instr_rend() {
      return instr_reverse_iterator(instr_begin());
    }
};

//----------------------------------------------------------------------------

class MachineSingleEntryPathRegion : public MachineSingleEntryRegion {

  public:

    // ctor for the path regions of machine basic blocks, this only acts as
    // a wrapper for the inherited parents ctor, without additional functio-
    // nality
    MachineSingleEntryPathRegion(const MachineFunction &parentParam,
                                 MachineBasicBlock &entryParam,
                                 const MBBListTy &blocksParam)
    : MachineSingleEntryRegion(parentParam, entryParam, blocksParam)
    {}

    // trivial region ctor
    MachineSingleEntryPathRegion(const MachineFunction &parentParam,
                                 MachineBasicBlock &MBBParam)
    : MachineSingleEntryRegion(parentParam, MBBParam)
    {}

    // in order to make the verification more specific (for paths) we need to
    // override the verification method inherited from the basic region-class
    virtual void verify();

    // this method "sorts" the list of basic blocks contained in the path-re-
    // gion starting with the entry and then continuing successor-wise
    virtual void sort();
};

//----------------------------------------------------------------------------

class MachineSingleEntryCFGRegion : public MachineSingleEntryRegion {

  public:

    // ctor for the iCFG regions of machine basic blocks, this only acts as
    // a wrapper for the inherited parents ctor, without additional functio-
    // nality
    MachineSingleEntryCFGRegion(const MachineFunction &parentParam,
                                MachineBasicBlock &entryParam,
                                const MBBListTy &blocksParam)
    : MachineSingleEntryRegion(parentParam, entryParam, blocksParam)
    {}

    // in order to make the verification more specific (for cfg-partss) we
    // need to override the verification method inherited from the basic
    // region-class
    virtual void verify();

    // this method "linearizes" the representation of the region, to keep
    // things simple, we perform a breadth-first-sort of the basic blocks,
    // so that blocks are found in the list later than their predecessors
    virtual void sort();
};

} // namespace llvm

#endif
