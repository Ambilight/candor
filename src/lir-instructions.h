#ifndef _SRC_LIR_INSTRUCTIONS_H_
#define _SRC_LIR_INSTRUCTIONS_H_

#include "lir.h"
#include "lir-inl.h"
#include "hir-instructions.h"
#include "hir-instructions-inl.h"
#include "macroassembler.h" // Label
#include "zone.h"
#include "utils.h"

namespace candor {
namespace internal {

// Forward declarations
class LInstruction;
class LBlock;
class ScopeSlot;
typedef ZoneList<LInstruction*> LInstructionList;

#define LIR_INSTRUCTION_TYPES(V) \
    V(Nop) \
    V(Label) \
    V(Nil) \
    V(Move) \
    V(Gap) \
    V(Entry) \
    V(Return) \
    V(Function) \
    V(LoadArg) \
    V(LoadContext) \
    V(StoreContext) \
    V(LoadProperty) \
    V(StoreProperty) \
    V(DeleteProperty) \
    V(Branch) \
    V(Literal) \
    V(Goto) \
    V(Not) \
    V(BinOp) \
    V(Typeof) \
    V(Sizeof) \
    V(Keysof) \
    V(Clone) \
    V(Call) \
    V(CollectGarbage) \
    V(GetStackTrace) \
    V(AllocateObject) \
    V(CloneObject) \
    V(AllocateArray) \
    V(Phi)

#define LIR_INSTRUCTION_ENUM(I) \
    k##I,

class LInstruction : public ZoneObject {
 public:
  enum Type {
    LIR_INSTRUCTION_TYPES(LIR_INSTRUCTION_ENUM)
    kNone
  };

  LInstruction(Type type) : type_(type),
                            id(-1),
                            input_count_(0),
                            scratch_count_(0),
                            has_call_(NULL),
                            block_(NULL),
                            slot_(NULL),
                            hir_(NULL),
                            propagated_(NULL) {
    inputs[0] = NULL;
    inputs[1] = NULL;
    scratches[0] = NULL;
    scratches[1] = NULL;

    result = NULL;
  }

  inline LInstruction* AddArg(LInterval* arg, LUse::Type use_type);
  inline LInstruction* AddArg(LInstruction* arg, LUse::Type use_type);
  inline LInstruction* AddArg(HIRInstruction* arg, LUse::Type use_type);

  inline LInstruction* AddScratch(LInterval* scratch);

  inline LInstruction* SetResult(LInterval* res, LUse::Type use_type);
  inline LInstruction* SetResult(LInstruction* res, LUse::Type use_type);
  inline LInstruction* SetResult(HIRInstruction* res, LUse::Type use_type);
  inline LInstruction* Propagate(LUse* res);
  inline LInstruction* Propagate(HIRInstruction* res);

  inline LInstruction* SetSlot(ScopeSlot* slot);

  inline LInstruction* MarkHasCall() { has_call_ = true; return this; }
  inline bool HasCall() { return has_call_; }

  inline Type type() { return type_; }
  inline LBlock* block() { return block_; }
  inline void block(LBlock* block) { block_ = block; }
  int id;

  static inline const char* TypeToStr(Type type);

  virtual void Print(PrintBuffer* p);

  int input_count() { return input_count_; }
  int result_count() { return result != NULL; }
  int scratch_count() { return scratch_count_; }

  inline HIRInstruction* hir() { return hir_; }
  inline void hir(HIRInstruction* hir) { hir_ = hir; }

  LUse* inputs[2];
  LUse* scratches[2];
  LUse* result;

 private:
  Type type_;
  int input_count_;
  int scratch_count_;
  bool has_call_;

  LBlock* block_;
  ScopeSlot* slot_;
  HIRInstruction* hir_;
  LUse* propagated_;
};

#undef LIR_INSTRUCTION_ENUM

class LLabel : public LInstruction {
 public:
  LLabel() : LInstruction(kLabel) {
  }

  static inline LLabel* Cast(LInstruction* instr);

  Label label;
};

class LGap : public LInstruction {
 public:
  class Pair : public ZoneObject {
   public:
    Pair(LInterval* from, LInterval* to) : from_(from), to_(to) {
    }

   private:
    LInterval* from_;
    LInterval* to_;

    friend class LGap;
  };

  typedef ZoneList<Pair*> PairList;

  LGap() : LInstruction(kGap) {
  }

  inline void Add(LInterval* from, LInterval* to);

  void Resolve();
  void Print(PrintBuffer* p);

  static inline LGap* Cast(LInstruction* instr);

 private:
  PairList unhandled_pairs_;
  PairList pairs_;
};

class LControlInstruction : public LInstruction {
 public:
  LControlInstruction(Type type) : LInstruction(type), target_count_(0) {
    targets_[0] = NULL;
    targets_[1] = NULL;
  }

  void Print(PrintBuffer* p);

  inline void AddTarget(LLabel* target);
  inline LLabel* TargetAt(int i);

  inline int target_count() { return target_count_; }

  static inline LControlInstruction* Cast(LInstruction* instr);

 private:
  int target_count_;
  LLabel* targets_[2];
};

class LGoto : public LControlInstruction {
 public:
  LGoto() : LControlInstruction(kGoto) {
  }
};

class LBranch : public LControlInstruction {
 public:
  LBranch() : LControlInstruction(kBranch) {
  }
};

class LLoadArg: public LInstruction {
 public:
  LLoadArg(int index) : LInstruction(kLoadArg), index_(index) {
  }

 private:
  int index_;
};

} // namespace internal
} // namespace candor

#endif // _SRC_LIR_INSTRUCTIONS_H_
