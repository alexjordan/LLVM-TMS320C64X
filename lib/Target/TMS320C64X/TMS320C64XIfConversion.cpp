//===-- TMS320C64XIfConversion.cpp - Profile guided if conversion pass. ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an if-conversion pass for the TMS320C64X target.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ifconversion"
#include "TMS320C64X.h"
#include "TMS320C64XTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineProfileAnalysis.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegions.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/SuperblockFormation.h"
#include "llvm/CodeGen/TailReplication.h"
#include "llvm/Target/TargetInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCSymbol.h"
#include <list>

using namespace llvm;

namespace llvm {
  enum ProfitEstimationType { Dynamic, Static };
}

static cl::opt<llvm::ProfitEstimationType>
EstimationType("ifconv-estimation", cl::Hidden,
  cl::desc("Choose how to perform if-conversion profitability estimation"),
  cl::init(Dynamic), cl::values(
    clEnumValN(Dynamic, "profile", "Estimate by inspecting profile info"),
    clEnumValN(Static, "static", "Inspect structure, estimate statically"), 
    clEnumValEnd));

static cl::opt<unsigned>
ExecThresh("ifconv-exec-thresh",
  cl::desc("Execution threshold for if-conversion candidate-blocks"),
  cl::init(5), cl::Hidden);

static cl::opt<double>
ConversionLimit("ifconv-func-limit",
  cl::desc("Restrict percentage of if-conversions for each function"),
  cl::init(0.25), cl::Hidden);

static cl::opt<bool>
AggressiveConversion("ifconv-aggressive",
  cl::desc("Convert aggressively as much convertibles as possible"),
  cl::init(false), cl::Hidden);

static cl::opt<bool>
RunOnSuperblocks("ifconv-superblocks-only",
  cl::desc("Run the if-conversion on machine superblocks only"),
  cl::init(false), cl::Hidden);

static cl::opt<bool>
RewriteCalls("ifconv-rewrite-calls",
  cl::desc("Transform calls into predicable branch instructions"),
  cl::init(false), cl::Hidden);

STATISTIC(NumRemovedBranchesStat, "Number of removed branch instructions");
STATISTIC(NumPredicatedBlocksStat, "Number of predicated basic blocks");
STATISTIC(NumDuplicatedBlocksStat, "Number of duplicated basic blocks");
STATISTIC(NumRewrittenCallsStat, "Number of rewritten call instructions");

//------------------------------------------------------------------------------

namespace {

// @enum CONVERSION_PREFERENCE: This enum is provided to improve readability
// of the code and is used to specify the preference for the conversion, i.e.
// which of both outcomes of a structure is to be preferred for conditional
// elimination, i.e. merging into the corresponding head block

enum CONVERSION_PREFERENCE {
  PREFER_FBB  = 0,
  PREFER_TBB  = 1,
  PREFER_ALL  = 2,
  PREFER_NONE = 3
};

//------------------------------------------------------------------------------

// @enum IF_STRUCTURE_TYPE: This enum is used to distinguish between various
// cfg patterns which are used for the if-conversion. We basically support
// open or closed variations of triangle and diamond patterns for the current
// implementation

enum IF_STRUCTURE_TYPE {
  BAD_IF_STRUCTURE  = 0,

  IF_TRIANGLE = 1,

  // For simplicity the FMBB is restricted to have at most 1 successor which
  // is the tail (TMBB). The number of predecessors is not limited. If FMBB
  // has more than 1 pred, it is tail-duplicated before conversion. The head
  // is not restricted, but the tail needs to have exactly 2 preds. We either
  // can merge FMBB into the head, collapse all three blocks together, or
  // disrupt the tail, creating an IF_OPEN construct if it seems to be more
  // promising

  /* IF_TRIANGLE:
        ...
         |
       head
      /   |
    FMBB  |
      \   |
       TMBB
         |
        ...
  */

  IF_DIAMOND = 2,

  // converting diamonds is slightly more complicated and usually less pro-
  // fitable. We again restrict one of the branch targets (FMBB/TMBB) to
  // have at most one successor. The number of preds is not limited. The
  // conversion procedure is similar to the procedure of convering IF_OPENs
  // with the difference, that we eventually can merge all blocks together,
  // which however is rarely profitable

  /* IF_DIAMOND:
       ...
        |
       head
      /   \
    FMBB TMBB
      \   /
       tail
        |
       ...
  */

  IF_OPEN = 3,

  // fortunately we can handle all variations of open-patters uniformly. We
  // restrict one of the blocks (FMBB or TMBB) to have exactly 1 successor,
  // the other block is not restricted. If this block (FMBB shown) has more
  // than 1 pred, it is eventually tail-duplicated before conversion. We con-
  // vert this structure by merging FMBB into the head and attaching (in the
  // function layout) TMBB to it. NOTE, that TMBB can also have no preds (it
  // is most probably a function exit), however we still convert it the same
  // way (predicating an exit node does not work well yet)

  /* IF_OPEN:
       ...
        |
       head
      /   \
    FMBB TMBB
     |   /  \ 
    ...   ...
  */
 
  NUM_IF_STRUCTURES = 4 
};

//------------------------------------------------------------------------------

// @struct BBInfo: To simplify analysis of particular basic blocks we provide
// a simple struct to record the relevant data for each basic block. We record
// simple facts like predicability/duplicability as well as the branch informa-
// tion if available

struct BBInfo {

  /// DATA

  MachineBasicBlock *MBB;

  bool conditional;
  bool analyzable;
  bool duplicable;
  bool predicable;
  bool predicated;

  unsigned size;
  unsigned cycles;

  MachineBasicBlock *branchTBB;
  MachineBasicBlock *branchFBB;
  SmallVector<MachineOperand, 4> branchCond;

  /// METHODS/ACCESSORS

  BBInfo(MachineBasicBlock *argMBB = 0)
  : MBB(argMBB),
    conditional(true),
    analyzable(true),
    duplicable(true),
    predicable(true),
    predicated(false),
    size(0),
    cycles(0),
    branchTBB(0),
    branchFBB(0)
  {}
};

//------------------------------------------------------------------------------

// @class IfConversionInfo - we use simple records to store valuable data for
// machine basic blocks being if-converted, such as execution frequency, pred
// info, etc. Additionally we keep track of any conversion actions suggested
// by the profitability analysis

struct IfConvertible {

  // structure identifier
  IF_STRUCTURE_TYPE type;

  // structure parts
  BBInfo headInfo;
  BBInfo TBBInfo;
  BBInfo FBBInfo;
  BBInfo tailInfo;

  IfConvertible(IF_STRUCTURE_TYPE T,
                BBInfo &head,
                BBInfo &TBB,
                BBInfo &FBB)
  : type(T),
    headInfo(head),
    TBBInfo(TBB),
    FBBInfo(FBB),
    tailInfo(BBInfo(0))
  {}

  IfConvertible(IF_STRUCTURE_TYPE T,
                BBInfo &head,
                BBInfo &TBB,
                BBInfo &FBB,
                BBInfo &tail)
  : type(T),
    headInfo(head),
    TBBInfo(TBB),
    FBBInfo(FBB),
    tailInfo(tail)
  {}
};

//------------------------------------------------------------------------------

typedef std::pair<unsigned, std::pair<unsigned, unsigned> > PHIEntryInfo;
typedef SmallVector<MachineOperand, 4> SmallPredVectorTy;

//------------------------------------------------------------------------------

// @class TMS320C64XIfConversion - Class for the profile-guided if-conversion
// pass for machine functions. Contains all the heavy machinery to extract,
// select, estimate and convert special if-patterns. Designed to be run on
// the machine-level, after the instruction selection, but before RA, so the
// SSA-formed code is assumed

class TMS320C64XIfConversion : public MachineFunctionPass {

  private:

    MachineProfileAnalysis *MPI;
    MachineLoopInfo *MLI;

    const TMS320C64XInstrInfo *TII;
    const InstrItineraryData *IID;

    // number of cycles for a branch
    const unsigned BRANCH_CYCLES;

    // this is a working list for all if-conversion candidates we have mana-
    // get to extract. This map is emptied and rebuild each iteration of the
    // if-converting algorithm (iterative). If profile information is availa-
    // ble, convertibles will be stored ascending by head's execution count
    std::multimap<unsigned, IfConvertible> candidateMap;

    // a temporary structure that maps machine basic blocks to their informa-
    // tion nodes. Each machine basic block meeting initial criteria will be
    // analyzed and stored within this map. The map is rebuilt each iteration
    std::map<MachineBasicBlock*, BBInfo> analyzedBlocks;

    // local function statistics
    unsigned NumRemovedBranches;
    unsigned NumPredicatedBlocks;
    unsigned NumDuplicatedBlocks;
    unsigned NumRewrittenCalls;

  public:

    static char ID;

    // for the init-macro to shut up
    TMS320C64XIfConversion();

    // actual object ctor used for creating a pass instance
    TMS320C64XIfConversion(TMS320C64XTargetMachine &tm);
    ~TMS320C64XIfConversion() {}

    // Require profile information for dynamic estimations only. Static esti-
    // mation is implemented to use the machine loop information instead
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<MachineLoopInfo>();
      AU.addRequired<MachineProfileAnalysis>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    virtual const char *getPassName() const {
      return "Profile guided if-conversion pass for the TMS320C64X target";
    }

    // one of the most popular functions within llvm. No further explanations   
    virtual bool runOnMachineFunction(MachineFunction &MF);

    // check and return a successor common to both specified basic blocks
    static MachineBasicBlock *getCommonSuccessor(MachineBasicBlock &A,
                                                 MachineBasicBlock &B);

    // check the specified machine basic block (MBB) and report whether it
    // has phi-instructions that contain values being defined within the spe-
    // cified source machine basic block
    static bool hasPHIEntriesForMBB(MachineBasicBlock *MBB,
                                    MachineBasicBlock *sourceMBB);

    // static helper, inspect the given machine phi-instruction for possible
    // values defined by the spefieid basic block. Upon success, return the
    // first index for the source register found
    static unsigned getPHISourceRegIndex(const MachineInstr &PHIInstr,
                                         MachineBasicBlock *sourceMBB);

    // specifies which of both branch outcomes (FBB or TBB) is to be prefered
    // for the condtional elimination. This block will subsequently be predi-
    // cated, adjusted and merged with the corresponding head block
    CONVERSION_PREFERENCE getConversionPreference(IfConvertible &IC);

    // provided for debugging purposes, this method prints the information
    // what machine basic blocks the given convertible structure consists of
    void printCandidate(const IfConvertible &candidate) const;

  private:

    // analyzes each machine basic block of the specified function, creates
    // a struct containing all the information relevant during the conver-
    // sion. All information structs are stored within a temp-map. The map
    // is rebuild by this function each iteration
    void analyzeMachineFunction(MachineFunction &MF);

    // given a list of already analyzed machine basic block and corresponding
    // information structs, this function tries to extract if-patterns. All
    // analyzed basic blocks are considered for the head of such a structure
    void extractFromMachineFunction();

    // alternatively, instead of converting the entire machine function, we
    // can restrict the pattern extraction on the created superblocks (if
    // any available). This is profitable for many cases and usually limits
    // the amount of additional duplication
    void extractFromSuperblocks();

    // inspects the given information struct about a machine basic block and
    // tries to extract a convertible pattern out of it, which is IF_OPEN,
    // IF_TRIANGLE or IF_DIAMONG. Each pattern suitable (in terms of the CFG)
    // is stored in a working list of convertible candidates
    void extractIfPattern(BBInfo &headBlock);

    // a handy helper whether it is profitable to duplicate the specified BB.
    // the estimation is done statically by inspecting the size of the block,
    // the number of its successors/predecessors, etc
    bool isProfitableToDuplicate(BBInfo &MBB) const;

    // another helper method that tells whether the specified source block
    // is mergeable into the specified destination block. I.e. whether its
    // CFG structure (predecessors/successors) is suitable (preds/succs/loop
    // backedges), whether it eventually can be duplicated, etc.
    bool canMergeBlock(BBInfo &srcBlockInfo, BBInfo &destBlockInfo) const;

    // additionally to 'canMergeBlock' this method checks whether specified
    // machine basic block can be predicated. This check is done for blocks
    // being candidates for a conversion
    bool canPredicateBlock(BBInfo &MBBInfo) const;

    // this helper method specifies whether we can skip predication of the
    // specified instruction. For example this is true for llvm-IR pseudo's
    // such as kill, imp-defs, or for target instrs such as special "labels"
    bool requiresPredication(const MachineInstr &MI) const;

    // this is another handy helper, which returns a side entry into 'tail',
    // different from specified predecessors 'skipPredA' and 'skipPredB', it
    // comes useful when duplicating basic blocks
    MachineBasicBlock *getSideEntryIntoTail(MachineBasicBlock *tail,
                                            MachineBasicBlock *skipPredA,
                                            MachineBasicBlock *skipPredB);

    // given a basic block (tail), inspect it for any phi-instructions that
    // have source values defined in the predecessors A and B. If yes, we do
    // remove them and return a set containing a phi-dest/source information
    // about all entries we have removed
    std::set<PHIEntryInfo> updatePHIs(BBInfo &A, BBInfo &B, BBInfo &tail);

    // after processing possible phi-instructions in a tail block, we need to
    // insert predicated copies into the specified block (BB) which is assumed
    // to be the only predecessor (most probably result of merging blocks) of
    // the tail. After inserting the copies, we have transformed all phi's
    // within the tail block without violating SSA properties of the code
    void insertPredCopies(BBInfo &BB,
                          std::set<PHIEntryInfo> &deadPhis,
                          SmallPredVectorTy &predicates);

    // during the merge of basic blocks, some funny side-effects can arise.
    // This includes unconditional branches being predicated and yet not
    // recognized as conditional without being transformed explicitly into
    // branch_cond's. Additionally, since the branch is now conditional, its
    // fallthrough is most probably wrong. I.e. we need to insert yet another
    // branch. Another example - if conditionally terminated MBBs are allowed
    // is predicating a conditional two-way branch, resulting in a three-way
    // branch now...fun.
    void patchTerminators(BBInfo &head, BBInfo &succ,
                          SmallPredVectorTy &P,
                          bool insertBranch = true);

    // given a list of predicates (we can only assign one predicate each ite-
    // ration for the time being), each instruction within specified block is
    // predicated. Already predicated instructions will be handled by folding
    // predicates/rewriting destinations
    void predicateBlock(BBInfo &MBB, SmallPredVectorTy &P);

    // this method merges the tail block into the specified head-block. The
    // terminator of the head is dropped, all instructions within the tail
    // cloned and appended to the head and its terminator updated. After this
    // the tail block is erased from the function
    void mergeBlocks(BBInfo &head, BBInfo &tail);

    // given basic block is duplicated, all side entries (predecessors) into
    // the block are removed with exception of the predecessor we explicitly
    // want to keep (skipPred)
    void duplicateBlock(MachineBasicBlock *MBB, MachineBasicBlock *skipPred);

    // experimental feature for rewriting TMS callp instructions into regular
    // branches (this will eventually increase the predication scope and does
    // make delay slots of calls explicit)
    void rewriteCallInstruction(MachineInstr &callMI);

    // to simplify the converting process, we eliminate fallthrough's by ma-
    // king each transition explicit via branch instructions. Some of them
    // will subsequently be removed, however, this simplifies the conversion-
    // handling considerably...
    void removeUnterminatedFallthrough(BBInfo &MBBI);

    // inspect each candidate in the priority queue and if all conversion-
    // restrictions and profitability estimations hold, try to conver them.
    // Report success upon conversion. When converting aggressively, the en-
    // tire function will be converted as long this method reports true
    bool convertStructure(IfConvertible &candidate);
};

char TMS320C64XIfConversion::ID = 0;

} // end of anonymous namespace

//------------------------------------------------------------------------------

INITIALIZE_PASS_BEGIN(TMS320C64XIfConversion,
  "pi-if-conversion", "Profile guided if-conversion pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_AG_DEPENDENCY(MachineProfileAnalysis)
INITIALIZE_PASS_END(TMS320C64XIfConversion, 
  "pi-if-conversion", "Profile guided if-conversion pass", false, false)

//------------------------------------------------------------------------------

FunctionPass *
llvm::createTMS320C64XIfConversionPass(TMS320C64XTargetMachine &tm) {
  return new TMS320C64XIfConversion(tm);
}

//------------------------------------------------------------------------------

TMS320C64XIfConversion::TMS320C64XIfConversion()
: MachineFunctionPass(ID),
  TII(0),
  IID(0),
  BRANCH_CYCLES(5)
{
  initializeTMS320C64XIfConversionPass(*PassRegistry::getPassRegistry());
}

//------------------------------------------------------------------------------

TMS320C64XIfConversion::TMS320C64XIfConversion(TMS320C64XTargetMachine &TM)
: MachineFunctionPass(ID),
  TII(TM.getInstrInfo()),
  IID(TM.getInstrItineraryData()),
  BRANCH_CYCLES(5)
{
  initializeTMS320C64XIfConversionPass(*PassRegistry::getPassRegistry());
}

//------------------------------------------------------------------------------
// printCandidate:
//
// A helper method handy for debugging purposes. It emits the name information
// about the specified if-convertible structure, i.e. names of the head/TBB/
// FBB/tail blocks the structure is built of

void
TMS320C64XIfConversion::printCandidate(const IfConvertible &candidate) const {

  assert(candidate.type != BAD_IF_STRUCTURE && "ERROR: bad if-structure!\n");

  dbgs() << "Parent function: " << candidate.headInfo.MBB->
    getParent()->getFunction()->getNameStr() << '\n';

  if (candidate.type == IF_TRIANGLE) dbgs() << "IF_TRIANGLE:\n";
  else if (candidate.type == IF_DIAMOND) dbgs() << "IF_DIAMOND:\n";
  else if (candidate.type == IF_OPEN) dbgs() << "IF_OPEN:\n";

  if (!candidate.headInfo.MBB) dbgs () << "  MBB: (null)\n";
  else dbgs() << "  MBB: " << candidate.headInfo.MBB->getName() << '\n';

  if (!candidate.TBBInfo.MBB) dbgs () << "  TBB: (null)\n";
  else dbgs() << "  TBB: " << candidate.TBBInfo.MBB->getName() << '\n';

  if (!candidate.FBBInfo.MBB) dbgs () << "  FBB: (null)\n";
  else dbgs() << "  FBB: " << candidate.FBBInfo.MBB->getName() << '\n';

  if (!candidate.tailInfo.MBB) dbgs () << "  tail: (null)\n";
  else dbgs() << "  tail: " << candidate.tailInfo.MBB->getName() << '\n';
  dbgs() << '\n';
}

//------------------------------------------------------------------------------
// getCommonSuccessor:
//
// This helper method returns the first successor which is preceeded (in terms
// of control flow) by both specified machine basic blocks. There can be more
// than 1 common succ, however, for our demands it is sufficient to return the
// first encountered one

MachineBasicBlock *
TMS320C64XIfConversion::getCommonSuccessor(MachineBasicBlock &FBB,
                                           MachineBasicBlock &TBB)
{
  if (!FBB.succ_size() || !TBB.succ_size()) return 0;

  MachineBasicBlock::succ_iterator SI;
  for (SI = FBB.succ_begin(); SI != FBB.succ_end(); ++SI)
    if (TBB.isSuccessor(*SI)) return *SI;
  return 0;
}

//------------------------------------------------------------------------------
// requiresPredication:
//
// Returns whenter a given machine instruction can be skipped during predi-
// cation process. This is true for some llvm-pseudo artifacts such as kill
// or implicit defs or target pseudos. Note, this actually also includes any
// IR copies, however we do handle them separately

bool TMS320C64XIfConversion::requiresPredication(const MachineInstr &MI) const
{
  switch (MI.getOpcode()) {
    case TargetOpcode::KILL:
    case TargetOpcode::IMPLICIT_DEF:
    case TMS320C64X::call_return_label:
      return false;
    default: return true;
  }
}

//------------------------------------------------------------------------------
// removeUnterminatedFallthrough:
// 
// To simplify some of the conversion steps, it is useful to remove any fall-
// throughs and make transitions between basic blocks explicit. This is done
// by appending an unconditional branch to the end of the specified block.
// The branch-destination is the layout successor within the parent function

void TMS320C64XIfConversion::removeUnterminatedFallthrough(BBInfo &infoMBB) {
  MachineBasicBlock *MBB = infoMBB.MBB;
  assert(MBB && "Cant remove fallthrough, bad block!");

  // can not handle, if already terminated. NOTE, that we only handle blocks
  // having no terminators at all, i.e. we skip conditionally
  if (MBB->getFirstTerminator() != MBB->end()) return;

  // track the position of the next block for the fallthrough transition
  MachineFunction::iterator nextMBB = ++MachineFunction::iterator(MBB);
  assert(nextMBB != MBB->getParent()->end() && "Invalid block position!");

  MachineInstr *branchMI = TII->addDefaultPred(BuildMI(*MBB,
    MBB->end(), DebugLoc(), TII->get(TMS320C64X::branch)).addMBB(nextMBB));
  DEBUG(dbgs() << "Removed fallthrough: " << *branchMI << '\n');
}

//------------------------------------------------------------------------------
// analyzeMachineFunction:
//
// iterate over all machine basic blocks of the given function, collect the
// information about it (such as predicable/predicated/num succs/preds, etc).
// If the block is not generally suitable for the further processing (such
// as when having cycles, or a low execution count) the block is skipped, if
// not, its instructions are inspected and the information used for the if-
// conversion generated (branch structure, duplicability/predicability, etc)
 
void TMS320C64XIfConversion::analyzeMachineFunction(MachineFunction &MF) {

  for (MachineFunction::iterator MBBI = MF.begin(); MBBI != MF.end(); ++MBBI) {

    MachineBasicBlock *MBB = &*MBBI;
    assert(MBB && "Bad block detected, something is really fishy here!");

    if (MF.getFunction()->getNameStr() == "main"
    && MBB->getName() == "do.body.i.i.i.i.i.i") {
      MBB->dump();
    }

    // do not permit trivial MBB cycles
    if (MBB->isSuccessor(MBB)) continue;

    if (EstimationType == Dynamic)
      if (MPI && MPI->getExecutionCount(MBB) < ExecThresh) continue;

    // this check is provided to skip "dead" blocks which do not seem to have
    // any predecessors and are not the entry blocks for the machine function
    if (MBBI->getBasicBlock() != &(MF.getFunction()->getEntryBlock()))
      if (!MBB->pred_size()) continue;

    BBInfo blockInfo(MBB);
    MachineBasicBlock::iterator MI;

    for (MI = MBB->begin(); MI != MBB->end(); ++MI) {
      if (MI->isDebugValue() || MI->isPHI()) continue;

      // also skip instructions, which can but do not need to be predicated.
      // this includes stuff like kill, implicit-def or target pseudos such
      // as returning pads for indirect jumps. This does not include PHIs or
      // copies, these will require special attention later
      if (!requiresPredication(*MI)) continue;

      // handle a copy separately, a copy will probably expand to a target-
      // move later, therefore we consider it for our estimaton heuristic by
      // comparing its execution cycles to a regular physical register move
      if (MI->getOpcode() == TargetOpcode::COPY) {
        blockInfo.cycles++;
        continue;
      }

      // we are not able to predicate inline-asm yet...
      if (MI->isInlineAsm()) {
        DEBUG(dbgs() << "Can't predicate asm-instruction: " << *MI);
        blockInfo.predicable = false;
      }

      const int op = MI->getOpcode();

      // we eventually can accept calls if they are rewrittable, thus we do
      // distinguish them explicitly from the rest of the instructions
      if (op == TMS320C64X::callp_global || op == TMS320C64X::callp_extsym) {
        if (!(RewriteCalls || AggressiveConversion)) {
          DEBUG(dbgs() << "Can't predicate call-instruction: " << *MI);
          blockInfo.predicable = false;
        }
      } else if (!TII->isPredicable(MI)) {
        DEBUG(dbgs() << "Can't predicate instruction: " << *MI);
        blockInfo.predicable = false;
      }

      const TargetInstrDesc &MID = MI->getDesc();

      // record if eventually we already have predicated this basic block
      // once. NOTE, the TMS-target uses predication for conditional jumps.
      // Thus, we do not consider any terminators and record non-terminators
      // only
      if (TII->isPredicated(MI)) {
        if (!MID.isConditionalBranch()) blockInfo.predicated = true;

        // when folding predicates we require a destination of each instruc-
        // tion to be a register, this of course doesn't apply to mem-stores.
        // This means, we can not further predicate an already predicated
        // store-instruction yet
        if (MID.mayStore()) blockInfo.predicable = false;
      }

      if (MID.hasUnmodeledSideEffects() || MID.isNotDuplicable())
        blockInfo.duplicable = false;

      // now estimate the number of execution cycles required for the current
      // machine basic block. We aquire this information from the target.
      blockInfo.cycles += TII->getInstrLatency(IID, MI);
      blockInfo.size++;
    }

    MachineBasicBlock *TBB = 0;
    MachineBasicBlock *FBB = 0;
    SmallVector<MachineOperand, 4> Cond;

    const bool isAnalyzable = !TII->AnalyzeBranch(*MBB, TBB, FBB, Cond, 0);

    blockInfo.conditional = Cond.size();
    blockInfo.analyzable = isAnalyzable;

    // if the branch is conditional, the target may fold the branch, if any
    // of the branch destinations is a layout follower. This makes one of the
    // TBB/FBB blocks 0. Check explicitly and assign target blocks properly
    if (blockInfo.conditional && isAnalyzable) {
      if (!TBB && !FBB) llvm_unreachable("Cond. branch without targets!");

      // make branch targets explicit for simplicity of analysis steps
      if (!FBB) FBB = llvm::next(MachineFunction::iterator(MBB));
      else if (!TBB) TBB = llvm::next(MachineFunction::iterator(MBB));

      // double check for sure, strange things happen when iterating across
      // parent function boundaries (llvm::next). So, check successors here,
      // they have to be within the same parent of course
      assert(TBB && FBB && "Incomplete targets for a conditional branch!");
      assert(MBB->isSuccessor(TBB) && MBB->isSuccessor(FBB) && "No mercy!");
    }

    blockInfo.branchTBB = TBB;
    blockInfo.branchFBB = FBB;
    blockInfo.branchCond = Cond;

    analyzedBlocks.insert(std::make_pair(MBB, blockInfo));
  }
}

//------------------------------------------------------------------------------
// extractFromMachineFunction:
//
// given a list of already analyzed machine basic block and corresponding
// information structs, this function tries to extract if-patterns. All ana-
// lyzed basic blocks are considered for the head of such a structure

void TMS320C64XIfConversion::extractFromMachineFunction() {
  std::map<MachineBasicBlock*, BBInfo>::iterator BI;
  for (BI = analyzedBlocks.begin(); BI != analyzedBlocks.end(); ++BI) {
    assert(BI->first && "Can not extract pattern, bad block detected!");
    extractIfPattern(BI->second);
  }
}

//------------------------------------------------------------------------------
// extractFromSuperblocks:
//
// alternatively, instead of converting the entire machine function, we can
// restrict the pattern extraction on the created superblocks (if any avai-
// lable). This can be profitable for many cases and usually limits the amount
// of code expansion when tail-duplicating

void TMS320C64XIfConversion::extractFromSuperblocks() {

  const MachineSuperBlockMapTy &superblocks =
    SuperblockFormation::getSuperblocks();

  if (!superblocks.size()) return;

  // extract in decreasing order, we start with the most frequent one
  for (MachineSuperBlockMapTy::const_iterator SB = superblocks.begin();
       SB != superblocks.end(); ++SB)
  {
    MachineSuperBlock *MSB = SB->second;
    assert(MSB && "Invalid basic block detected within a superblock!");

    // inspect the superblock-blocks now
    MachineSuperBlock::iterator MBBI;
    for (MBBI = MSB->begin(); MBBI != MSB->end(); ++MBBI) {
      std::map<MachineBasicBlock*, BBInfo>::iterator
        blockInfoPos = analyzedBlocks.find(*MBBI);

      if (blockInfoPos != analyzedBlocks.end())
        extractIfPattern(blockInfoPos->second);
    }
  }
}

//------------------------------------------------------------------------------
// extractIfPattern:
//
// Given an already (properly) analyzed machine basic block, this function
// tries to extract a convertible if-pattern out of it. It does not pay too
// much attention to profitability yet, but simply checks for general conver-
// sion constraints

void TMS320C64XIfConversion::extractIfPattern(BBInfo &head) {

  if (!head.conditional || !head.analyzable || head.MBB->succ_size() != 2)
    return;

  // check for the targets, both of them must have been set propertly
  assert(head.branchTBB && head.branchFBB && "Bad branch-targets!");
  assert(head.branchTBB != head.branchFBB && "Strange double-edge!");

  // there must not be cycles between branch targets
  if (head.branchTBB->isSuccessor(head.branchFBB)
  && (head.branchFBB->isSuccessor(head.branchTBB))) return;

  // also, we can not fold nodes properly yet, whose both branch outcomes
  // are leafs (usually function-exit blocks). Folding such nodes seem to
  // result in heavy inconsistencies within llvm, therefore, skip them now
  if (!head.branchTBB->succ_size() && !head.branchFBB->succ_size())
    return;

  std::map<MachineBasicBlock*, BBInfo>::iterator
    TBBI = analyzedBlocks.find(head.branchTBB),
    FBBI = analyzedBlocks.find(head.branchFBB);

  if (TBBI == analyzedBlocks.end() || FBBI == analyzedBlocks.end())
    return;

  unsigned headCount = MPI ? MPI->getExecutionCount(head.MBB) : 0;

  BBInfo infoTBB = TBBI->second;
  BBInfo infoFBB = FBBI->second;

  /// now after doing the initial preselection, we can try to extract conver-
  /// sion patterns. If both outcomes share a common successor, we then are
  /// dealing with a diamond. If one of the branch targets is a successor of
  /// another one then we have a triangle. All other cases result in varia-
  /// tions of open patterns

  if (infoTBB.MBB->isSuccessor(infoFBB.MBB)
  || infoFBB.MBB->isSuccessor(infoTBB.MBB)) {

    // usually, the tail is TBB, and (mostly fallthrough) block FBB. Reverse
    // positions if this is not the case (which won't happen frequent but is
    // still possible)
    if (infoTBB.MBB->isSuccessor(infoFBB.MBB)) {
      BBInfo tmp = infoTBB;
      infoTBB = infoFBB;
      infoFBB = tmp;
    }

    // FBB is neither predicable nor mergeable, therefore give up...
    if (!(canPredicateBlock(infoFBB) && canMergeBlock(infoTBB, head)))
      return;

    // in case we require a duplication of the convMBB-block, the tail block
    // will get an additional predecessor. We limit the number of total side
    // entries into the tail to 1, i.e. the max number of preds to be 3
    unsigned maxNumSideEntries = 2 + (infoFBB.MBB->pred_size() == 1);
    if (infoTBB.MBB->pred_size() > maxNumSideEntries) return;
                
    candidateMap.insert(std::make_pair(headCount,
      IfConvertible(IF_TRIANGLE, head, infoTBB, infoFBB)));
  }
  else {

    MachineBasicBlock *tailMBB =
      getCommonSuccessor(*infoTBB.MBB, *infoFBB.MBB);

    const bool canConvertTBB =
      canPredicateBlock(infoTBB) && canMergeBlock(infoTBB, head);

    const bool canConvertFBB =
      canPredicateBlock(infoFBB) && canMergeBlock(infoFBB, head);

    // tough luck, give it up and go home, crusader
    if (!canConvertTBB && !canConvertFBB) return;

    if (!tailMBB) {
      candidateMap.insert(std::make_pair(headCount, 
        IfConvertible(IF_OPEN, head, infoTBB, infoFBB)));
    }
    else {
      // since we are going to predicate both blocks, require both of them to
      // be mergeable and predicable. for the time being we only allow simple
      // conversions, collapsing all blocks into the head
      if (!canConvertTBB || !canConvertFBB) return;

      // if there is a common successor-block, we obviously have a diamond
      // here. Converting diamonds is slightly more complicated and conver-
      // ting them entirely is rarely being very profitable, we probably
      // can gain most of the conversion by doing stepwise, i.e. we first
      // merge one of the outcomes into the head, thus creating a triangle,
      // and then check whether it is profitable to reduce this triangle
      // into a straight line code
      std::map<MachineBasicBlock*, BBInfo>::iterator
        tailMBBI = analyzedBlocks.find(tailMBB);

      if (tailMBBI == analyzedBlocks.end()) return;

      BBInfo &tail = tailMBBI->second;

      // i hope, checking for FBB is sufficient...
      if (!canMergeBlock(tail, infoFBB)) return;

      const unsigned maxNumSideEntries =
        (infoTBB.MBB->pred_size() > 1) + (infoFBB.MBB->pred_size() > 1);

      // here too, we only handle 1 side entry into the tail right now. This
      // tail entry can result from duplication of the either the FBB or TBB
      // block, or exist 'naturally' without any duplication
      if (tailMBB->pred_size() + maxNumSideEntries > 3) return;

      candidateMap.insert(std::make_pair(headCount,
        IfConvertible(IF_DIAMOND, head, infoTBB, infoFBB, tail)));
    }
  }
}

//------------------------------------------------------------------------------
// canMergeBlock:
//
// A simple helper method to determine whether we can generally merge speci-
// fied block (src) into another one (dst). For this to be true, the block is
// required to have some basic properties as well as the ability to be dupli-
// cable eventually

bool
TMS320C64XIfConversion::canMergeBlock(BBInfo &srcInfo, BBInfo &dstInfo) const {

  if (!srcInfo.MBB || !srcInfo.analyzable) return false;
  if (srcInfo.MBB->getBasicBlock()->hasAddressTaken()) return false;

  // in case of side-entries, we eventually can convert the structure by
  // removing them via a tail-duplication. I.e. require MBB to be duplicable
  if ((srcInfo.MBB->pred_size() > 1) && !srcInfo.duplicable) return false;

  // additionally we avoid merging parent-loop header blocks into the head,
  // this complicates matters when it comes to phi-adjustment and is rarely
  // really profitable, therefore reject such cases
  assert(MLI && "Can't check for backedges without loop-info!");
  assert(srcInfo.MBB && dstInfo.MBB && "Invalid backedge blocks!");

  if (MLI->isLoopHeader(srcInfo.MBB)) {
    // this check is probably sufficient for nat. loops...
    const int dstLoopDepth = MLI->getLoopDepth(dstInfo.MBB);
    const int srcLoopDepth = MLI->getLoopDepth(srcInfo.MBB);
    return srcLoopDepth <= dstLoopDepth;
  }
  return true;
}

//------------------------------------------------------------------------------
// canPredicateBlock:
//
// Another helper similar to 'canMergeBlock'. This one checks whether we can
// predicate the given machine basic block

bool TMS320C64XIfConversion::canPredicateBlock(BBInfo &MBBInfo) const {
  // for simplicity we also limit the number of successors...
  return MBBInfo.predicable && MBBInfo.MBB->succ_size() == 1;
}

//------------------------------------------------------------------------------
// hasPHIEntriesForMBB:
//
// A simple helper method for checking whether there are any phi-instructions
// within 'MBB' that have source values defined in 'sourceMBB'. These checks
// may come very handy especially when doing a conversion of triangles and
// beating/converting phi-instructions

bool TMS320C64XIfConversion::hasPHIEntriesForMBB(MachineBasicBlock *MBB,
                                                 MachineBasicBlock *sourceMBB)
{
  if (!MBB || !sourceMBB) return false;

  MachineBasicBlock::iterator MI;
  for (MI = MBB->begin(); MI != MBB->end() && MI->isPHI(); ++MI)
    if (getPHISourceRegIndex(*MI, sourceMBB)) return true;
  return false;
}

//------------------------------------------------------------------------------
// getSideEntryIntoTail:
//
// This method is used to aquire a side entry basic block into the specified
// tail, which is useful when converting triangles. A side entry block has to
// be different from both of the specified predecessors

MachineBasicBlock*
TMS320C64XIfConversion::getSideEntryIntoTail(MachineBasicBlock *tailMBB,
                                             MachineBasicBlock *skipPredA,
                                             MachineBasicBlock *skipPredB)
{
  MachineBasicBlock::pred_iterator PI;
  for (PI = tailMBB->pred_begin(); PI != tailMBB->pred_end(); ++PI)
    if (*PI != skipPredA && *PI != skipPredB && *PI != tailMBB) return *PI;
  return 0;
}

//------------------------------------------------------------------------------
// isProfitableToDuplicate:
//
// Estimate whether it is profitable to duplicate the specified block. For the
// time being, we use a simple static heuristic for the estimation in order to
// control the code size expansion. NOTE, this is most probably best to be mo-
// ved to the TargetInstrInfo file in future

bool TMS320C64XIfConversion::isProfitableToDuplicate(BBInfo &blockInfo) const {
  if (!blockInfo.duplicable) return false;

  // avoid inserting too many additional branches
  if (blockInfo.MBB->succ_size() > 4) return false;

  // avoid duplicating really big blocks
  if (blockInfo.size > 64) return false;

  // restrict the number of duplicated blocks to 20 percent for now...
  const double growthRatio = blockInfo.MBB->getParent()->size() * 0.2;
  if (NumDuplicatedBlocks > growthRatio) return false;

  return true;
}

//------------------------------------------------------------------------------
// getPHISourceRegIndex:
//
// Another handy helper when fucking around with phi-instructions during con-
// version. The specified machine instruction 'PHIInstr' is inspected for any
// incoming values from 'sourceMBB'. If one can be found, its operand-index
// is returned. Otherwise the result is 0. NOTE, this is the same implemen-
// tation that is currently found within CodeGen/TailReplication and is most
// probably best removed in future

unsigned
TMS320C64XIfConversion::getPHISourceRegIndex(const MachineInstr &PHIInstr,
                                             MachineBasicBlock *sourceMBB)
{
  assert(PHIInstr.isPHI() && "Can only process phi-instructions!");

  // look for the operand index for the register corresponding to the speci-
  // fied machine basic block within the specified machine instruction
  for (unsigned I = 1; I < PHIInstr.getNumOperands(); I += 2)
    if (PHIInstr.getOperand(I + 1).getMBB() == sourceMBB) return I;
  return 0;
}

//------------------------------------------------------------------------------
// getConversionPreference:
//
// Given a specified info struct about a basic block (which is assumed to be
// a head of a convertible structure, its branching structure is inspected
// and analyzed with respect to the profitability/execution counts. Depending
// on this analysis a suggestion is given whether and what to convert

CONVERSION_PREFERENCE
TMS320C64XIfConversion::getConversionPreference(IfConvertible &candidate) {

  BBInfo &headBBI = candidate.headInfo;
  BBInfo &infoFBB = candidate.FBBInfo;
  BBInfo &infoTBB = candidate.TBBInfo;

  if (candidate.type == BAD_IF_STRUCTURE) return PREFER_NONE;
  assert(headBBI.MBB && infoFBB.MBB && infoTBB.MBB && "Invalid blocks!");

  // for the time being we can only convert blocks which have 1 succ
  if (infoFBB.MBB->succ_size() != 1 && infoTBB.MBB->succ_size() != 1)
    return PREFER_NONE;

  double execFreqFBB = 0.0;
  double execFreqTBB = 0.0;
  double execFreqHead = 0.0;

  // for a static estimation we only require the machine-loop-analysis to be
  // present. We inspect the program structure and decide upon block proper-
  // ties such as being a loop header, nesting level, etc.
  if (EstimationType == Static) {
    if (!MLI) return PREFER_NONE;

    MachineBasicBlock *FBB = infoFBB.MBB;
    MachineBasicBlock *TBB = infoTBB.MBB;

    // estimate exec. frequency (prefer deeper nested blocks)
    execFreqFBB = (MLI->getLoopDepth(FBB) * 100.0) + 1.0;
    execFreqTBB = (MLI->getLoopDepth(TBB) * 100.0) + 1.0;

    MachineLoop *mlFBB = MLI->getLoopFor(FBB);
    MachineLoop *mlTBB = MLI->getLoopFor(TBB);

    // backedges to the headers are usually more frequent than non-backedges,
    // NOTE, that we do not yet prefer header-blocks over non-header blocks
    if (mlFBB && FBB->isSuccessor(mlFBB->getHeader())) execFreqFBB *= 10.0;
    if (mlTBB && TBB->isSuccessor(mlTBB->getHeader())) execFreqTBB *= 10.0;

    // finally, consider predecessor num
    execFreqFBB *= (FBB->pred_size() + 1.0);
    execFreqTBB *= (TBB->pred_size() + 1.0);
    execFreqHead = execFreqFBB + execFreqTBB;
  }
  else {
    // for a profile based estimation, we use basic block counts for now, but
    // will probably change later to use profiled weights for edges between
    // machine basic blocks
    if (!MPI) return PREFER_NONE;

    execFreqFBB = MPI->getExecutionCount(infoFBB.MBB);
    execFreqTBB = MPI->getExecutionCount(infoTBB.MBB);
    execFreqHead = MPI->getExecutionCount(headBBI.MBB);
  }

  if (candidate.type == IF_OPEN) {

    // estimating open patterns is rather simple, since we can only merge
    // one branch target into the head and append the other to this combo.
    // I use a simple formula here and hope it to be effective...
    if (BRANCH_CYCLES * execFreqFBB > execFreqTBB * infoFBB.cycles)
      if (canPredicateBlock(infoFBB) && canMergeBlock(infoFBB, headBBI))
        return PREFER_FBB;

    // now try another way around, check whether TBB is profitable
    if (BRANCH_CYCLES * execFreqTBB > execFreqFBB * infoTBB.cycles)
      if (canPredicateBlock(infoTBB) && canMergeBlock(infoTBB, headBBI))
        return PREFER_TBB;
  }
  else if (candidate.type == IF_TRIANGLE) {

    // estimating triangles is a slightly different story, for now we only
    // consider opportunities where we can merge both blocks (FBB and tail
    // into the head). NOTE, we always refer the tail block as TBB here !
    unsigned cycleLimit = infoFBB.cycles;
    if (EstimationType == Static) cycleLimit /= 2.0;

    // more restrictive for dynamic estimations, static estimations usually
    // seem to profit from less tight bounds (obtained experimentally...)
    if (execFreqHead * BRANCH_CYCLES > execFreqTBB * cycleLimit)
      return PREFER_ALL;
  }
  else if (candidate.type == IF_DIAMOND) {

    // diamonds are rarely being really profitable. To simplify matters, we
    // only consider diamond-structures which either can completely be col-
    // lapsed into a single block, or disrupted by splitting the tail block
    const double penalty = execFreqTBB * infoFBB.cycles +
                           execFreqFBB * infoTBB.cycles;

    if (execFreqHead * BRANCH_CYCLES > penalty / 1.5) return PREFER_ALL;
  }
  return PREFER_NONE;
}

//------------------------------------------------------------------------------
// duplicateBlock:
//
// The specified basic block (i.e. tail) will be duplicated (@TailReplication)
// so all edges except from the specified predecessor (pred) will be redirec-
// ted to the cloned block. TODO, additionally, the profiling information will
// be adjusted, so the conversion can continue plausibly after the duplication

void TMS320C64XIfConversion::duplicateBlock(MachineBasicBlock *tail,
                                            MachineBasicBlock *pred)
{
  assert(tail && pred && "Can not duplicate, bad blocks specified!");

  TailReplication tailReplicator(TII);
  tailReplicator.duplicateTail(*pred, *tail);

  assert(tail->pred_size() == 1 && "Unallowed side entries found!");
  DEBUG(dbgs() << "Duplicated block: " << tail->getName() << '\n');

  ++NumDuplicatedBlocks;
}

//------------------------------------------------------------------------------
// updatePHIs:
//
// When converting triangles, it may become necessary to update phi-instruc-
// tions of the tail-block. If the tail block contains phi-entries for the
// FBB and the head, and if both are merged by the conversion, we then need
// to deal with a situation of phi-nodes having different source values from
// the same source block.
//
// regX = phi(regY:blockTBB, regZ:blockFBB)
//
// To allow merging 'blockTBB' and 'blockFBB' we save the destination 'regX'
// together with the source values 'regY' and 'regZ' appropriately within a
// temp set. Later conversion steps will then use this information and per-
// form something like this:
//
// [ pred] regX = regY
// [!pred] regX = regZ
//
// The original phi-instruction can then be dropped completely. Using a
// pseudo-instruction for this mutually-exclusive predicated move allows us
// to avoid the single-assignment restriction and offers a semantic equiva-
// lent to a two-entry-phi node, without beating yet with a full psi-SSA
// implementation

std::set<PHIEntryInfo> TMS320C64XIfConversion::updatePHIs(BBInfo &sourceTBB,
                                                          BBInfo &sourceFBB,
                                                          BBInfo &tailInfo)
{
  MachineBasicBlock *MBB = tailInfo.MBB;
  MachineBasicBlock *TBB = sourceTBB.MBB;
  MachineBasicBlock *FBB = sourceFBB.MBB;

  assert(MBB && TBB && FBB && "Can not rewrite phi's, bad blocks!");

  std::set<PHIEntryInfo> replacedPhisInfo;
  std::list<MachineInstr*> deadPhis;

  MachineBasicBlock::iterator MI;
  for (MI = MBB->begin(); MI != MBB->end() && MI->isPHI(); ++MI) {

    unsigned indexTBB = getPHISourceRegIndex(*MI, FBB);
    unsigned indexFBB = getPHISourceRegIndex(*MI, TBB);

    if (indexTBB && indexFBB) {

      // get the destination of the phi instruction. Additionally we create
      // an entry for the later update. While we could have done it here in-
      // line, we dont, to improve the readability and structure of the code
      unsigned destReg = MI->getOperand(0).getReg();
      unsigned valueTBB = MI->getOperand(indexTBB).getReg();
      unsigned valueFBB = MI->getOperand(indexFBB).getReg();

      PHIEntryInfo PEI(destReg, std::make_pair(valueTBB, valueFBB));
      assert(!replacedPhisInfo.count(PEI) && "Multiple phi's to remove!");
      replacedPhisInfo.insert(PEI);

      // we expect the phi-instruction to have entries for sourceA/B only !
      // Then, we can safely remove the current phi instruction from the MBB
      assert(MI->getNumOperands() < 6 && "Bad phi-instruction detected!");
      deadPhis.push_back(MI);
    }
  }

  // now drop all phi-instructions from the tail block that now are "dead".
  // NOTE, this can make the basic blocks empty ! Be sure to run a DCE or
  // something similar after this !
  for(std::list<MachineInstr*>::iterator FIRST_DEAD = deadPhis.begin(),
      LAST_DEAD = deadPhis.end(); FIRST_DEAD != LAST_DEAD; ++FIRST_DEAD)
  {
    DEBUG(dbgs() << "Erased phi-instruction: " << *FIRST_DEAD);
    (*FIRST_DEAD)->eraseFromParent();
  }

  return replacedPhisInfo;
}

//------------------------------------------------------------------------------
// rewriteCallInstruction:
//
// For some experiments i provide a way to morph a subset of call instruc-
// tions as supported by the current TI-backend into a branching instruction.
// This has the advantage of the parent block to become predicable eventually,
// and also makes delay slots explicit which then can be filled by the sched.

void TMS320C64XIfConversion::rewriteCallInstruction(MachineInstr &callMI) {

  const int op = callMI.getOpcode();
  assert(op == TMS320C64X::callp_global || op == TMS320C64X::callp_extsym);

  MachineBasicBlock &MBB = *callMI.getParent();
  MachineModuleInfo &MMI = MBB.getParent()->getMMI();

  // destination operand for the call instruction
  MachineOperand &targetMO = callMI.getOperand(0);

  // generate a unique label for the return from the call, the address of
  // the label will be put into B3 and supplied to the callee to be able
  // to return from the subroutine
  const char *retPad = MMI.getContext().CreateTempSymbol()->getName().data();

  // generate a move instruction for the return label, this is done by
  // creating a double move (mvkl/mvkh) as usual. Pay attention to define
  // reg operand properly, since we assume to be dealing with machine SSA!
  MachineBasicBlock::iterator pos(&callMI);

  // we add a dummy side-specified in order to make the codegen shut up and
  // stop complaining about number of operand inconsistencies much later...
  TMS320C64XInstrInfo::addFormOp(TII->addDefaultPred(BuildMI(
    MBB, pos, DebugLoc(), TII->get(TMS320C64X::mvkl_2)).addReg(TMS320C64X::B3,
      RegState::Define).addExternalSymbol(retPad)), TMS320C64XII::unit_s, 0);

  // now do the same stuff with the higher portion of the target address
  TMS320C64XInstrInfo::addFormOp(TII->addDefaultPred(BuildMI(
    MBB, pos, DebugLoc(), TII->get(TMS320C64X::mvkh_2)).addReg(TMS320C64X::B3,
      RegState::Define).addExternalSymbol(retPad).addReg(TMS320C64X::B3)),
        TMS320C64XII::unit_s, 0);

  MachineInstr *branchMI = 0;

  if (targetMO.isGlobal()) {
    const GlobalValue *GA = targetMO.getGlobal();
    assert(GA && "Invalid global address for a call detected!");

    // now rewrite the original call into a pseudo "branch" instruction
    branchMI = TII->addDefaultPred(BuildMI(MBB, ++pos, DebugLoc(),
      TII->get(TMS320C64X::call_branch)).addGlobalAddress(GA));
  }
  else if (targetMO.isSymbol()) {
    const char *SYM = targetMO.getSymbolName();
    assert(SYM && "Bad name for an external symbol detected!");

    branchMI = TII->addDefaultPred(BuildMI(MBB, ++pos, DebugLoc(),
      TII->get(TMS320C64X::call_branch)).addExternalSymbol(SYM));
  } else llvm_unreachable("Dead code, must not happen at all!");

  // create a label to be loaded into B3 as a return address
  BuildMI(MBB, pos, DebugLoc(), TII->get(TMS320C64X::call_return_label))
    .addExternalSymbol(retPad);

  // now to finalize rewriting we need to patch in the call arguments proper-
  // ly. This is for sure a very ugly way to morph a call into a pseudo branch,
  // however, it is sufficient for our experiments now and will eventually be
  // improved later...

  // skip the header of the call instruction, together with the call target
  // and predication operands (target=0, predImm=1, predReg=2). Since a call
  // (as a callp instruction) can not be predicated, this can be improved in
  // the instruction description, TODO, FIXME

  if (callMI.getNumOperands() < 4) return;

  // now simply transfer the current operand...
  for (unsigned I = 3; I < callMI.getNumOperands(); ++I)
    branchMI->addOperand(callMI.getOperand(I));

  // last but not least patch in a B3 register operand as a return address-
  // operand. This has to be done in order to avoid B3 defs being eliminated
  branchMI->addOperand(MachineOperand::CreateReg(TMS320C64X::B3, false));
  DEBUG(dbgs() << "Morphed call: " << callMI << " into: " << *branchMI);
}

//------------------------------------------------------------------------------
// insertPredCopies:
//
// When a tail block has phi-values from two predecessors, we need to handle
// the predication properly, because when merging predecessors there will be
// multiple source values incoming from one source block. To solve this prob-
// lem we remove the phi-entries from the instruction and insert predicated
// copies into the merged predecessor. See 'updatePHIs' 

void TMS320C64XIfConversion::insertPredCopies(BBInfo &blockInfo,
                                              std::set<PHIEntryInfo> &deadPhis,
                                              SmallPredVectorTy &predicates)
{
  MachineBasicBlock *MBB = blockInfo.MBB;
  assert(blockInfo.MBB && "Can not insert copies, bad basic block!");

  // now iterate over the specified set of "dead" phi instruction infos, get
  // the values for the former destinations and source values and insert pre-
  // dicated pseudo-copies for each of them
  std::set<PHIEntryInfo>::iterator PI;
  for (PI = deadPhis.begin(); PI != deadPhis.end(); ++PI) {

    const unsigned destReg = PI->first;
    const unsigned sourceRegTBB = PI->second.first;
    const unsigned sourceRegFBB = PI->second.second;

    // now create a new predicated reg-copy for the given set of registers,
    // and append it to the end of the specified block (before the termina-
    // tor). We need to pay attention to predicate the instruction properly!
    MachineInstr *predSelectMI = BuildMI(*MBB, MBB->getFirstTerminator(),
      DebugLoc(), TII->get(TMS320C64X::mvselect), destReg)
        .addReg(sourceRegTBB).addReg(sourceRegFBB);

    if (!TII->PredicateInstruction(predSelectMI, predicates))
      DEBUG(dbgs() << "Can not predicate pred-move MI" << *predSelectMI);

    DEBUG(dbgs() << "Inserted a pred-reg-select MI: " << *predSelectMI);
  }
}

//------------------------------------------------------------------------------
// predicateBlock:
//
// Given a basic block information and a list of predicates (right now we are
// only able to assign one single predicate at a time) each instruction in the
// block is predicated. Special care must be taken, if there are instructions
// already predicated (in this case we fold predicates by 'selecting' them),
// and also in cases, when the block is (unconditionally) terminated. Predi-
// cating the branch instruction will semantically turn it into a conditional
// branch, i.e. we may need to pay attention to the following fallthrough...

void TMS320C64XIfConversion::predicateBlock(BBInfo &blockInfo,
                                            SmallPredVectorTy &predicates)
{
  assert(blockInfo.MBB && "Can not if-convert, bad basic block found!");
  DEBUG(dbgs() << "Predicating block: " << blockInfo.MBB->getName() << '\n');

  MachineBasicBlock &MBB = *blockInfo.MBB;
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  std::list<MachineInstr*> rewrittenCalls;

  /// now iterate over all instructions in the block and assign predicates
  /// to them. If we have inserted custom select-pred instructions above, we
  /// need to skip them, since we have attached a predicated to them already.
  /// also, if the block ends with a conditional branch, we also leave it un-
  /// touched, also due to the predicated folding performed earlier

  for (MachineBasicBlock::iterator MI = MBB.begin(); MI != MBB.end(); ++MI) {
    if (MI->isDebugValue() || MI->isPHI() || MI->isInlineAsm()) continue;

    const int op = MI->getOpcode();

    // we also skip any llvm-intermed. pseudos such as for example kill.
    // also skip any pseudo-real machine instructions (such as ret-labels)
    if (!requiresPredication(*MI) || op == TargetOpcode::COPY) continue;

    if (MI->getDesc().isConditionalBranch()) continue;

    // now eventually rewrite call instructions if desired, check for consis-
    // tency for sure, the rewriting is only allowed when desired explicitly,
    // or via an aggressive if-conversion
    if (op == TMS320C64X::callp_global || op == TMS320C64X::callp_extsym) {
      assert((RewriteCalls || AggressiveConversion)
        && "Inconsistent pattern selection, not rejected yet?");
      rewriteCallInstruction(*MI);
      rewrittenCalls.push_back(MI);
      continue;
    }

    if (TII->isPredicated(MI)) {
      DEBUG(dbgs() << "Predicated MI found: " << *MI << '\n');

      /// in case we encounter an instruction which is already predicated, we
      /// basically need to decide, since we can do two things. We can connect
      /// involved predicates (already used one and a new one we are going to
      /// assign) logically by and-/or-ing. This is good when there are few
      /// large pools of already predicated instructions using different pre-
      /// dicates, since this increases pressure on predicate-registers. This
      /// is bad if there are only few of them. Alternatively, you can "rew-
      /// rite" instructions to write into a new destination register and then
      /// insert a predicated move which transfers the content into the orig.
      /// destination. This effectively doubles the number of predicated inst-
      /// ructions and therefore increases register pressure on the GPR's.
      /// For the time being we implement the second variant, while still ex-
      /// perimenting with another alternative, TODO

      const int predIndex = MI->findFirstPredOperandIdx();
      assert(predIndex != -1 && "Predicated MI without a predicate!");

      // used predicate must not interfere with the predicate which we have
      // assigned to the block
      MachineOperand &predRegMO = MI->getOperand(predIndex + 1);
      assert(predRegMO.getReg() != predicates[1].getReg());

      // create a new virtual register as a destination for the instruction
      // and rewrite the instruction, save the old register for later
      MachineOperand &destRegMO = MI->getOperand(0);
      assert(destRegMO.isReg() && "Cant find a destination register!");

      const unsigned oldDestReg = destRegMO.getReg();
      const TargetRegisterClass *RC = MRI.getRegClass(oldDestReg);
      unsigned newDestReg = MRI.createVirtualRegister(RC);
      destRegMO.setReg(newDestReg);

      // now insert a copy instruction which simply copies the newly created
      // destination register into the old one
      MachineBasicBlock::iterator insertPos = MI;
      MachineInstr *predMoveMI = BuildMI(MBB, ++insertPos, DebugLoc(),
        TII->get(TargetOpcode::COPY), oldDestReg).addReg(newDestReg);

      DEBUG(dbgs() << "Folded predicate MI: " << *predMoveMI);
    }
    else if (!TII->PredicateInstruction(&*MI, predicates))
      DEBUG(dbgs() << "Can't predicate instruction: " << *MI);
  }

  // now if we have rewritten any calls, drop original call-instructions, we
  // collect them first in a temp-set and drop them after the predication, it
  // does not look most elegant, but simplifies implementation
  while (rewrittenCalls.size()) {
    rewrittenCalls.back()->eraseFromParent();
    rewrittenCalls.pop_back();
    NumRewrittenCalls++;
  }

  NumPredicatedBlocks++;
}

//------------------------------------------------------------------------------
// mergeBlocks:
//
// Given two basic blocks we merge the 'tail' into the 'head'. This is done by
// cloning each instruction, updating the successor-information and also up-
// dating the phi-information for each succesor. After this procedure the tail
// block is dropped from the parent function completely and is gone forever

void TMS320C64XIfConversion::mergeBlocks(BBInfo &head, BBInfo &tail) {

  MachineBasicBlock *headMBB = head.MBB, *tailMBB = tail.MBB;
  assert(headMBB && tailMBB && "Bad machine blocks detected!");

  // again, we require the block which is to be duplicated to have no side-
  // entries, this should have been rejected earlier during the analysis...
  assert(tailMBB->pred_size() <= 1 && "Unallowed side entry detected!");

  // drop the branching instruction, and count properly
  if ((headMBB->getFirstTerminator() != headMBB->end())
  && (!headMBB->getFirstTerminator()->getDesc().isReturn())) {
    TII->RemoveBranch(*headMBB);
    ++NumRemovedBranches;
  }

  if (headMBB->isSuccessor(tailMBB)) headMBB->removeSuccessor(tailMBB);

  // leaving things up to the codegen for correction is probably not going to
  // work well, therefore we correct things manually here. Next, we duplica-
  // te all instructions from the tail into the head block
  MachineBasicBlock::iterator tailMI;
  for (tailMI = tailMBB->begin(); tailMI != tailMBB->end(); ++tailMI) {

    // in some cases, when after the duplication the tail contains phi's with
    // just one source value, we fold these phi-instructions into reg copies
    if (tailMI->isPHI()) {

      assert(tailMI->getNumOperands() == 3 && "Malformed phi-instruction!");
      unsigned destReg = tailMI->getOperand(0).getReg();
      unsigned srcReg = tailMI->getOperand(1).getReg();

      // insert a soft-register copy in-place of the phi-instruction
      MachineInstr *copyMI = BuildMI(*headMBB, headMBB->end(), DebugLoc(),
        TII->get(TargetOpcode::COPY), destReg).addReg(srcReg);

      DEBUG(dbgs() << "Replaced a phi by a copy-MI: " << *copyMI);
    }
    else {
      MachineFunction &MF = *(headMBB->getParent());
      MachineInstr *newHeadMI = TII->duplicate(tailMI, MF);
      headMBB->push_back(newHeadMI);
    }
  }

  // next, a successor-update is necessary. All successors of the tail block
  // are changed now to succeed the head instead. Can not change them in-line,
  // therefore we copy them ahead
  SmallVector<MachineBasicBlock*, 32> Succs(
    tailMBB->succ_begin(), tailMBB->succ_end());

  for (unsigned SI = 0; SI < Succs.size(); ++SI) {
    MachineBasicBlock *succ = Succs[SI];

    // transfer unique successor blocks, avoid double edges. Additionally we
    // are required to update any phi nodes for the side exits of the tail,
    // the entries originally containing values for the tail block will now
    // contain values for the head
    if (!headMBB->isSuccessor(succ)) {
      headMBB->addSuccessor(succ);

      MachineBasicBlock::iterator succMI;
      for (succMI = succ->begin(); succMI != succ->end(); ++succMI) {
        if (!succMI->isPHI()) continue;

        // just to ensure consistency, no single phi-entries from the head
        assert(!getPHISourceRegIndex(*succMI, headMBB) && "Bad boy dump!");

        // find the operand source for the tail within the phi-instruction
        const unsigned phiIndex = getPHISourceRegIndex(*succMI, tailMBB);
        if (!phiIndex) continue;

        DEBUG(dbgs() << "Adjusting successor's phi-node for the"
                     << " machine instruction: " << *succMI);

        MachineOperand &mbbMO = succMI->getOperand(phiIndex + 1);
        assert(mbbMO.isMBB() && "Bad phi operand (not a machine block)!");
        mbbMO.setMBB(headMBB);
      }
    }

    tailMBB->removeSuccessor(succ);
  }

  tailMBB->eraseFromParent();
}

//------------------------------------------------------------------------------
// patchTerminators:
//
// After two blocks have been merged we eventually have to update the termi-
// nator structure of the resulting block. This includes converting any pre-
// dicated branch instructions into explicit conditional branches and then
// inserting (if desired) an unconditional branch to the specified successor

void TMS320C64XIfConversion::patchTerminators(BBInfo &infoMBB,
                                              BBInfo &infoSucc,
                                              SmallPredVectorTy &predicates,
                                              bool insertExplicitBranch)
{
  MachineBasicBlock *MBB = infoMBB.MBB;
  MachineBasicBlock *succ = infoSucc.MBB;
  assert(MBB && succ && "Can not patch terminators, invalid blocks!");

  // note, that we assume the head basic block to be terminated...
  assert(MBB->getFirstTerminator() != MBB->end() && "No fallthrough's!");

  // in case we have predicated a block, which was terminated by an uncondi-
  // tional branch, we now have turned it into an unconditional one which
  // uses a predicate. Semantically, this is conditional now, however, to
  // make llvm understand this, we need to make it explicitly. I.e. we con-
  // vert an unconditional branch with a predicate into a conditional one,
  // i.e. branch [pred] -> branch_cond [pred]. TODO, consider predicated re-
  // gister branches too !
  if (MBB->size() && MBB->back().getOpcode() == TMS320C64X::branch) {
    MachineBasicBlock *TBB = MBB->back().getOperand(0).getMBB();
    assert(TBB && "Bad branch operand detected!");
    MBB->back().eraseFromParent();

    // addition, if insertion of an explicit terminating branch is desired,
    // and if the successor-block is the same as the destination of the
    // conditional branch, we drop the last and simply insert an uncondi-
    // tional branch to the successor. Such a case will happen frequently
    // when converting triangles
    if (TBB != infoSucc.MBB) {
      MachineInstr *branchMI = BuildMI(*MBB, MBB->end(), DebugLoc(),
        TII->get(TMS320C64X::branch_cond)).addMBB(TBB)
          .addImm(predicates[0].getImm()).addReg(predicates[1].getReg());
      DEBUG(dbgs() << "Rewritten predicated branch: " << *branchMI);
    }
  }

  // to express the relationship between blocks explicitly, if desired, we do
  // insert an unconditional branch to the specified successor. This removes
  // fallthroughs but allows us to move blocks freely during subsequent con-
  // version-iterations
  if (insertExplicitBranch) {
    assert(MBB->isSuccessor(succ) && "Must branch to a successor!");

    MachineInstr *branchMI = TII->addDefaultPred(BuildMI(*MBB,
      MBB->end(), DebugLoc(), TII->get(TMS320C64X::branch)).addMBB(succ));
    DEBUG(dbgs() << "Inserted unconditional branch: " << *branchMI);
  }
}

//------------------------------------------------------------------------------
// convertStructure:
//
// This is the main function where the entire conversion machinery is initia-
// ted. Given a conversion-candidate we distinguish between different patterns
// and if the profitability is given, try to convert it accordingly. For some
// patterns there are various conversion ways, to keep things simple we only
// consider the most important/promising ones

bool TMS320C64XIfConversion::convertStructure(IfConvertible &candidate) {

  // now ask the smart lil guy what he would do with the current structure.
  // for open structures, we basically can convert the true and/or false
  // machine basic blocks but not both
  CONVERSION_PREFERENCE preference = getConversionPreference(candidate);

  SmallVector<MachineOperand, 4> Cond = candidate.headInfo.branchCond;
  assert(Cond.size() && Cond[0].isImm() && Cond[1].isReg());

  BBInfo &head = candidate.headInfo;

  if (candidate.type == IF_DIAMOND) {

    /// usually, it is rarely profitable to convert diamonds into a straight
    /// line code segment. For the time being we only support the collapsing
    /// of the diamond into one single block. Only max 1 side-entry is allo-
    /// wed into the tail (which will be duplicated)

    if (preference != PREFER_ALL) return false;

    BBInfo &convTBB = candidate.TBBInfo;
    BBInfo &convFBB = candidate.FBBInfo;
    BBInfo &tail = candidate.tailInfo;

    assert(convFBB.MBB->succ_size() == 1 && convTBB.MBB->succ_size() == 1);
    assert(convFBB.MBB->pred_size() == 1 || convTBB.MBB->pred_size() == 1);
    assert(tail.MBB->pred_size() <= 3 && "Can only handle 1 side entry!");

    BBInfo sideEntry = convTBB;
    if (convFBB.MBB->pred_size() > 1) sideEntry = convFBB;

    if (sideEntry.MBB->pred_size() > 1 && tail.MBB->pred_size() < 3) {
      if (isProfitableToDuplicate(sideEntry)
      && isProfitableToDuplicate(tail)) {
        // duplicate both basic blocks now
        duplicateBlock(sideEntry.MBB, head.MBB);
        duplicateBlock(tail.MBB,
          getSideEntryIntoTail(tail.MBB, convFBB.MBB, convTBB.MBB));
        return true;
      } else return false;
    }

    // can not collapse tail if there are any side entries. Therefore dupli-
    // cate it before conversion. To keep things simple, we do not handle the
    // clone analysis inline, simply signal for a start of a new iteration
    if (tail.MBB->pred_size() > 2) {
      if (isProfitableToDuplicate(tail)) {
        // duplicate tail only
        duplicateBlock(tail.MBB,
          getSideEntryIntoTail(tail.MBB, convTBB.MBB, convFBB.MBB));
        return true;
      } else return false;
    }

    // we need to be careful to reverse the predicates if necessary...
    if (head.branchTBB == convFBB.MBB) Cond[0].setImm(!Cond[0].getImm());

    predicateBlock(convTBB, Cond);

    // reverse and predicate TBB now...
    Cond[0].setImm(!Cond[0].getImm());
    predicateBlock(convFBB, Cond);

    // if the tail does have any phi-entries from both specified blocks, we
    // need to remove them, since both blocks are going to be collapsed la-
    // ter. Collect information about dropped phi-instructions !
    std::set<PHIEntryInfo> deadPhis = updatePHIs(convTBB, convFBB, tail);

    // if there are any of dead phi-instructions, we remove them and insert
    // predicated-copy pseudo instruction to the block we intend to merge
    // (last !) into the head (in our case FBB)
    if (deadPhis.size()) insertPredCopies(convFBB, deadPhis, Cond);

    // now do the merging stuff
    mergeBlocks(head, convTBB);
    mergeBlocks(head, convFBB);
    mergeBlocks(head, tail);
    head.MBB->updateTerminator();
    return false;
  }
  else if (candidate.type == IF_OPEN) {

    if (preference != PREFER_FBB && preference != PREFER_TBB)
      return false;

    /// here we have a structure with both branch outcomes not sharing any
    /// immediate successors. We can convert either of the both branch tar-
    /// gets and need to select the most profitable one

    BBInfo conv = candidate.TBBInfo;
    BBInfo skip = candidate.FBBInfo;

    if (preference == PREFER_FBB) {
      conv = candidate.FBBInfo;
      skip = candidate.TBBInfo;
    }

    assert(conv.MBB->succ_size() == 1 && "Bad IF-OPEN struct!");

    // for simplicity insert an explicit branch if the candidate block is
    // falling through to another one and has no terminator instructions at
    // all. This complicates the uniform handling a bit, therefore, remove
    removeUnterminatedFallthrough(conv);

    if (conv.MBB->pred_size() > 1) {
      if (isProfitableToDuplicate(conv)) {

        if (conv.MBB->getName() == "do.body.i.i.i.i.i.i")
          conv.MBB->getParent()->viewCFGOnly();

//          dbgs() << "duplicating DO.BODY !!!!\n";

        duplicateBlock(conv.MBB, head.MBB);
        return true;
      } else return false;
    }

    // do a double check, there must not be any phi-entries from one branch-
    // alternative in the other, i.e. convBBI must not contain any phi values
    // from skipBBI and vice-versa
    assert(!hasPHIEntriesForMBB(conv.MBB, skip.MBB)
      && !hasPHIEntriesForMBB(skip.MBB, conv.MBB)
      && "Uncle Willie says...Phi or not to phi");

    // we need to be careful to reverse the predicates if necessary...
    if (head.branchTBB == skip.MBB) Cond[0].setImm(!Cond[0].getImm());

    // now run conversion steps
    predicateBlock(conv, Cond);
    mergeBlocks(head, conv);
    patchTerminators(head, skip, Cond);
    return true;
  }
  else if (candidate.type == IF_TRIANGLE) {

    /// for the time being to keep things simple, we only consider cases for
    /// a conversion where we can merge both branch targets into the head.
    /// This assumes a profitability being given as well as a number of suc-
    /// cessors/predecessors being suitable

    if (preference != PREFER_ALL) return false;

    BBInfo conv = candidate.FBBInfo;
    BBInfo tail = candidate.TBBInfo;

    assert(tail.MBB->pred_size() <= 3 && "Too many preds for the tail!");

    // we eventually will hav to duplicate the conv and the tail block, we do
    // it atomically for efficiency. NOTE, that duplicating 'conv' will indu-
    // ce an additional side-entry into the tail !
    if (conv.MBB->pred_size() > 1 && tail.MBB->pred_size () < 3) {
      if (isProfitableToDuplicate(conv) && isProfitableToDuplicate(tail)) {
        // duplicate both basic blocks now
        duplicateBlock(conv.MBB, head.MBB);
        duplicateBlock(tail.MBB,
          getSideEntryIntoTail(tail.MBB, conv.MBB, head.MBB));

        return true;
      } else return false;
    }

    // here we duplicate the tail only, i.e. in case when there are no side
    // entries into the conv block, but one side entry exists into the tail
    if (tail.MBB->pred_size() > 2) {
      if (isProfitableToDuplicate(tail)) {
        // duplicate tail only
        duplicateBlock(tail.MBB,
          getSideEntryIntoTail(tail.MBB, conv.MBB, head.MBB));

        return true;
      } else return false;
    }

    // usually, the branch goes to the tail, i.e. the tail being the TRUE-
    // destination and the conv-block being the FALSE (in most of the cases
    // a fallthrough) one. However, sometimes due to heavy conversions, there
    // can be opposite cases, this requires us to reverse the predicate value
    if (head.branchTBB == tail.MBB) Cond[0].setImm(!Cond[0].getImm());

    predicateBlock(conv, Cond);

    // the same stuff as for diamond-conversion, update phis here
    std::set<PHIEntryInfo> deadPhis = updatePHIs(head, conv, tail);
    if (deadPhis.size()) insertPredCopies(conv, deadPhis, Cond);

    // NOTE, tail unpredicated
    mergeBlocks(head, conv);
    mergeBlocks(head, tail);
    head.MBB->updateTerminator();
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------

bool TMS320C64XIfConversion::runOnMachineFunction(MachineFunction &MF) {

  NumRemovedBranches = 0;
  NumPredicatedBlocks = 0;
  NumDuplicatedBlocks = 0;
  NumRewrittenCalls = 0;

  if (EstimationType == Dynamic) {
    // for a dynamic profitability estimation we use a profile-information,
    // therefore we assume the information to be available, if none is
    // we give up and do not convert anything
    MPI = getAnalysisIfAvailable<MachineProfileAnalysis>();
    if (!MPI || !MPI->getExecutionCount(&MF)) return false;
  }

  MLI = getAnalysisIfAvailable<MachineLoopInfo>();
  if (!MLI) return false;

  DEBUG(dbgs() << "Run 'TMS320C64XIfConversion' pass for '"
               << MF.getFunction()->getNameStr() << "'\n");

  // get the percentage of maximum allowed conversions
  double conversionPercentLimit = ConversionLimit;
  if (conversionPercentLimit < 0 || conversionPercentLimit > 1.0)
    conversionPercentLimit = 0.25;

  unsigned maxConversions = MF.size() * ConversionLimit;
  bool continueConversion = true;

  while (continueConversion) {

    candidateMap.clear();
    analyzedBlocks.clear();

    // extract basic information about basic blocks, such as contained in-
    // structions, number of successors/predecessors, conditional termina-
    // tors, used predicates, etc.
    analyzeMachineFunction(MF);

    if (RunOnSuperblocks) extractFromSuperblocks();
    else extractFromMachineFunction();

    // now iterate over all identified convertible structures (triangles,
    // diamonds, etc.) and try to if-convert them if the conversion seems
    // to be profitable
    std::multimap<unsigned, IfConvertible>::reverse_iterator J;

    bool conversionDone = false;

    for (J = candidateMap.rbegin(); J != candidateMap.rend(); ++J) {
      if (convertStructure(J->second)) {
        DEBUG(dbgs() << "Converted:\n"; printCandidate(J->second));
        conversionDone = true;
        break;
      }
    }

    if (!conversionDone) break;

    // convert aggressively/exhaustively if desired to do so, otherwise limit
    // the number of conversions per function to the number specified on the
    // command line by the user...
    if (AggressiveConversion) continueConversion = true;
    else continueConversion = --maxConversions > 0;
  }

  // update the global if-conversion statistics
  NumRemovedBranchesStat += NumRemovedBranches;
  NumPredicatedBlocksStat += NumPredicatedBlocks;
  NumDuplicatedBlocksStat += NumDuplicatedBlocks;
  NumRewrittenCallsStat += NumRewrittenCalls;
  return NumRemovedBranches || NumDuplicatedBlocks;
}
  
