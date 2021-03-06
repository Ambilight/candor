/**
 * Copyright (c) 2012, Fedor Indutny.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <unistd.h>  // intptr_t

#include "lir.h"
#include "lir-inl.h"
#include "lir-instructions.h"
#include "lir-instructions-inl.h"
#include "macroassembler.h"
#include "stubs.h"  // Stubs

namespace candor {
namespace internal {

// Masm helpers

Register LUse::ToRegister() {
  assert(is_register());
  return RegisterByIndex(interval()->index());
}


Operand* LUse::ToOperand() {
  assert(is_stackslot());

  // Argc and return address
  return new Operand(rbp, -HValue::kPointerSize * (interval()->index() + 3));
}

#define __ masm->

void LLabel::Generate(Masm* masm) {
  __ bind(this->label);
}

void LEntry::Generate(Masm* masm) {
  __ bind(label_);
  __ push(rbp);
  __ mov(rbp, rsp);

  // Allocate spills
  __ AllocateSpills();

  // Save argc
  Operand argc(rbp, -HValue::kPointerSize * 2);
  __ mov(argc, rax);

  // Allocate context slots
  __ AllocateContext(context_slots_);
}


void LReturn::Generate(Masm* masm) {
  __ mov(rsp, rbp);
  __ pop(rbp);
  __ ret(0);
}


void LNop::Generate(Masm* masm) {
  // No need to generate real nops, they're only clobbering alignment
}


void LMove::Generate(Masm* masm) {
  // Ignore nop moves
  if (result->IsEqual(inputs[0])) return;
  __ Move(result, inputs[0]);
}


void LPhi::Generate(Masm* masm) {
  // Phi is absolutely the same thing as Nop
  // (it's here just for semantic meaning)
}


void LGap::Generate(Masm* masm) {
  // Resolve loops
  Resolve();

  PairList::Item* head = pairs_.head();
  for (; head != NULL; head = head->next()) {
    Pair* p = head->value();
    __ Move(p->dst_, p->src_);
  }
}


void LNil::Generate(Masm* masm) {
  if (result->instr() == this) return;

  __ Move(result, Immediate(Heap::kTagNil));
}


void LLiteral::Generate(Masm* masm) {
  if (result->instr() == this) return;

  if (root_slot_->is_immediate()) {
    __ Move(result,
            Immediate(reinterpret_cast<intptr_t>(root_slot_->value())));
  } else {
    assert(root_slot_->is_context());
    assert(root_slot_->depth() == -2);
    Operand slot(root_reg, HContext::GetIndexDisp(root_slot_->index()));
    __ Move(result, slot);
  }
}


void LAllocateObject::Generate(Masm* masm) {
  __ push(Immediate(HNumber::Tag(size_)));
  __ pushb(Immediate(HNumber::Tag(Heap::kTagObject)));
  __ Call(masm->stubs()->GetAllocateObjectStub());
}


void LAllocateArray::Generate(Masm* masm) {
  __ push(Immediate(HNumber::Tag(size_)));
  __ pushb(Immediate(HNumber::Tag(Heap::kTagArray)));
  __ Call(masm->stubs()->GetAllocateObjectStub());
}


void LGoto::Generate(Masm* masm) {
  __ jmp(TargetAt(0)->label);
}


void LBranch::Generate(Masm* masm) {
  // Coerce value to boolean first
  __ Call(masm->stubs()->GetCoerceToBooleanStub());

  // Jmp to `right` block if value is `false`
  Operand bvalue(rax, HBoolean::kValueOffset);
  __ cmpb(bvalue, Immediate(0));
  __ jmp(kEq, TargetAt(1)->label);
}


void LBranchNumber::Generate(Masm* masm) {
  Register reg = inputs[0]->ToRegister();
  Label heap_number, done;

  __ IsUnboxed(reg, &heap_number, NULL);

  __ cmpq(reg, Immediate(HNumber::Tag(0)));
  __ jmp(kEq, TargetAt(1)->label);
  __ jmp(TargetAt(0)->label);

  __ bind(&heap_number);
  Operand value(reg, HNumber::kValueOffset);
  __ movd(xmm1, value);
  __ xorqd(xmm2, xmm2);
  __ ucomisd(xmm1, xmm2);
  __ jmp(kEq, TargetAt(1)->label);

  __ bind(&done);
}


void LLoadProperty::Generate(Masm* masm) {
  Label done;
  Masm::Spill rax_s(masm, rax);

  // rax <- object
  // rbx <- propery
  __ mov(rcx, Immediate(0));
  if (HasMonomorphicProperty()) {
    __ Call(masm->space()->CreatePIC());
  } else {
    __ Call(masm->stubs()->GetLookupPropertyStub());
  }

  __ IsNil(rax, NULL, &done);
  rax_s.Unspill(rbx);
  Operand qmap(rbx, HObject::kMapOffset);
  __ mov(rbx, qmap);
  __ addq(rax, rbx);

  Operand slot(rax, 0);
  __ mov(rax, slot);

  __ bind(&done);
}


void LStoreProperty::Generate(Masm* masm) {
  Label done;
  Masm::Spill rax_s(masm, rax);
  Masm::Spill rcx_s(masm, rcx);

  // rax <- object
  // rbx <- propery
  // rcx <- value
  __ mov(rcx, Immediate(1));
  if (HasMonomorphicProperty()) {
    __ Call(masm->space()->CreatePIC());
  } else {
    __ Call(masm->stubs()->GetLookupPropertyStub());
  }

  // Make rax look like unboxed number to GC
  __ dec(rax);
  __ CheckGC();
  __ inc(rax);

  __ IsNil(rax, NULL, &done);
  rax_s.Unspill(rbx);
  rcx_s.Unspill(rcx);
  Operand qmap(rbx, HObject::kMapOffset);
  __ mov(rbx, qmap);
  __ addq(rax, rbx);

  Operand slot(rax, 0);
  __ mov(slot, rcx);

  __ bind(&done);
}


void LDeleteProperty::Generate(Masm* masm) {
  // rax <- object
  // rbx <- property
  __ Call(masm->stubs()->GetDeletePropertyStub());
}

#define BINARY_SUB_TYPES(V) \
    V(Add) \
    V(Sub) \
    V(Mul) \
    V(Div) \
    V(Mod) \
    V(BAnd) \
    V(BOr) \
    V(BXor) \
    V(Shl) \
    V(Shr) \
    V(UShr) \
    V(Eq) \
    V(StrictEq) \
    V(Ne) \
    V(StrictNe) \
    V(Lt) \
    V(Gt) \
    V(Le) \
    V(Ge)

#define BINARY_SUB_ENUM(V)\
    case BinOp::k##V: stub = masm->stubs()->GetBinary##V##Stub(); break;


void LBinOp::Generate(Masm* masm) {
  char* stub = NULL;

  switch (HIRBinOp::Cast(hir())->binop_type()) {
    BINARY_SUB_TYPES(BINARY_SUB_ENUM)
    default: UNEXPECTED
  }

  assert(stub != NULL);

  // rax <- lhs
  // rbx <- rhs
  __ Call(stub);
  // result -> rax
}


void LBinOpNumber::Generate(Masm* masm) {
  BinOp::BinOpType type = HIRBinOp::Cast(hir())->binop_type();

  Register left = rax;
  Register right = rbx;
  Register scratch = scratches[0]->ToRegister();
  Label stub_call, done;

  __ IsUnboxed(left, &stub_call, NULL);
  __ IsUnboxed(right, &stub_call, NULL);

  // Save left side in case of overflow
  __ mov(scratch, left);

  switch (type) {
    case BinOp::kAdd:
      __ addq(left, right);
      break;
    case BinOp::kSub:
      __ subq(left, right);
      break;
    case BinOp::kMul:
      __ Untag(left);
      __ imulq(right);
      break;
    default:
      UNEXPECTED
  }

  __ jmp(kNoOverflow, &done);

  // Restore left side
  __ mov(left, scratch);

  __ bind(&stub_call);

  char* stub = NULL;
  switch (type) {
    BINARY_SUB_TYPES(BINARY_SUB_ENUM)
    default: UNEXPECTED
  }
  assert(stub != NULL);

  // rax <- lhs
  // rbx <- rhs
  __ Call(stub);
  // result -> rax

  __ bind(&done);
}

#undef BINARY_SUB_ENUM
#undef BINARY_SUB_TYPES

void LFunction::Generate(Masm* masm) {
  // Get function's body address from relocation info
  __ mov(scratches[0]->ToRegister(), Immediate(0));
  RelocationInfo* addr = new RelocationInfo(RelocationInfo::kAbsolute,
                                            RelocationInfo::kQuad,
                                            masm->offset() - 8);
  label_->AddUse(masm, addr);

  // Call stub
  __ push(Immediate(HNumber::Tag(arg_count_)));
  __ push(scratches[0]->ToRegister());
  __ Call(masm->stubs()->GetAllocateFunctionStub());
}


void LCall::Generate(Masm* masm) {
  Label not_function, even_argc, done;

  // argc * 2
  __ mov(scratch, rax);

  __ testb(scratch, Immediate(HNumber::Tag(1)));
  __ jmp(kEq, &even_argc);
  __ addqb(scratch, Immediate(HNumber::Tag(1)));
  __ bind(&even_argc);
  __ shl(scratch, Immediate(2));
  __ addq(scratch, rsp);
  Masm::Spill rsp_s(masm, scratch);

  // rax <- argc
  // rbx <- fn

  __ IsUnboxed(rbx, NULL, &not_function);
  __ IsNil(rbx, NULL, &not_function);
  __ IsHeapObject(Heap::kTagFunction, rbx, &not_function, NULL);

  Masm::Spill ctx(masm, context_reg), root(masm, root_reg);
  Masm::Spill fn_s(masm, rbx);

  // rax <- argc
  // scratch <- fn
  __ mov(scratch, rbx);
  __ CallFunction(scratch);

  // Reset all registers to nil
  __ mov(scratch, Immediate(Heap::kTagNil));
  __ mov(rbx, scratch);
  __ mov(rcx, scratch);
  __ mov(rdx, scratch);
  __ mov(r8, scratch);
  __ mov(r9, scratch);
  __ mov(r10, scratch);
  __ mov(r11, scratch);
  __ mov(r12, scratch);
  __ mov(r13, scratch);

  fn_s.Unspill();
  root.Unspill();
  ctx.Unspill();

  __ jmp(&done);
  __ bind(&not_function);

  __ mov(rax, Immediate(Heap::kTagNil));

  __ bind(&done);

  // Unwind all arguments pushed on stack
  rsp_s.Unspill(rsp);
}


void LLoadArg::Generate(Masm* masm) {
  Operand slot(scratch, 0);

  Label oob, skip;

  // NOTE: input is aligned number
  __ mov(scratch, inputs[0]->ToRegister());

  // Check if we're trying to get argument that wasn't passed in
  Operand argc(rbp, -HValue::kPointerSize * 2);
  __ cmpq(scratch, argc);
  __ jmp(kGe, &oob);

  __ addqb(scratch, Immediate(HNumber::Tag(2)));
  __ shl(scratch, Immediate(2));
  __ addq(scratch, rbp);
  __ Move(result, slot);

  __ jmp(&skip);
  __ bind(&oob);

  // NOTE: result may have the same value as input
  __ Move(result, Immediate(Heap::kTagNil));

  __ bind(&skip);
}


void LLoadVarArg::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetLoadVarArgStub());
}


void LStoreArg::Generate(Masm* masm) {
  // Calculate slot position
  __ mov(scratch, rsp);
  __ shl(inputs[1]->ToRegister(), Immediate(2));
  __ addq(scratch, inputs[1]->ToRegister());
  __ shr(inputs[1]->ToRegister(), Immediate(2));

  Operand slot(scratch, 0);
  __ mov(slot, inputs[0]->ToRegister());
}


void LStoreVarArg::Generate(Masm* masm) {
  __ mov(rdx, rsp);
  __ shl(rbx, Immediate(2));
  __ addq(rdx, rbx);

  // eax - value
  // edx - offset
  __ Call(masm->stubs()->GetStoreVarArgStub());
}


void LAlignStack::Generate(Masm* masm) {
  Label even;
  __ testb(inputs[0]->ToRegister(), Immediate(HNumber::Tag(1)));
  __ jmp(kEq, &even);
  __ pushb(Immediate(Heap::kTagNil));
  __ bind(&even);

  // Now allocate space on-stack for arguments
  __ mov(scratch, inputs[0]->ToRegister());
  __ shl(scratch, Immediate(2));
  __ subq(rsp, scratch);
}


void LLoadContext::Generate(Masm* masm) {
  int depth = slot()->depth();

  if (depth == -1) {
    // Global object lookup
    Operand global(root_reg, HContext::GetIndexDisp(Heap::kRootGlobalIndex));
    __ mov(result->ToRegister(), global);
    return;
  }

  __ mov(result->ToRegister(), context_reg);

  // Lookup context
  while (--depth >= 0) {
    Operand parent(result->ToRegister(), HContext::kParentOffset);
    __ mov(result->ToRegister(), parent);
  }

  Operand res(result->ToRegister(),
              HContext::GetIndexDisp(slot()->index()));
  __ mov(result->ToRegister(), res);
}


void LStoreContext::Generate(Masm* masm) {
  int depth = slot()->depth();

  // Global can't be replaced
  if (depth == -1) return;

  __ mov(scratches[0]->ToRegister(), context_reg);

  // Lookup context
  while (--depth >= 0) {
    Operand parent(scratches[0]->ToRegister(), HContext::kParentOffset);
    __ mov(scratches[0]->ToRegister(), parent);
  }

  Operand res(scratches[0]->ToRegister(),
              HContext::GetIndexDisp(slot()->index()));
  __ mov(res, inputs[0]->ToRegister());
}


void LNot::Generate(Masm* masm) {
  // rax <- value

  // Coerce value to boolean first
  __ Call(masm->stubs()->GetCoerceToBooleanStub());

  Label on_false, done;

  // Jmp to `right` block if value is `false`
  Operand bvalue(rax, HBoolean::kValueOffset);
  __ cmpb(bvalue, Immediate(0));
  __ jmp(kEq, &on_false);

  Operand truev(root_reg, HContext::GetIndexDisp(Heap::kRootTrueIndex));
  Operand falsev(root_reg, HContext::GetIndexDisp(Heap::kRootFalseIndex));

  // !true = false
  __ mov(rax, falsev);

  __ jmp(&done);
  __ bind(&on_false);

  // !false = true
  __ mov(rax, truev);

  __ bind(&done);

  // result -> rax
}


void LTypeof::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetTypeofStub());
}


void LSizeof::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetSizeofStub());
}


void LKeysof::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetKeysofStub());
}


void LClone::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetCloneObjectStub());
}


void LCollectGarbage::Generate(Masm* masm) {
  __ Call(masm->stubs()->GetCollectGarbageStub());
}


void LGetStackTrace::Generate(Masm* masm) {
  AbsoluteAddress addr;

  addr.Target(masm, masm->offset());

  // Pass ip
  __ mov(rax, Immediate(0));
  addr.Use(masm, masm->offset() - 8);
  __ Call(masm->stubs()->GetStackTraceStub());
}

}  // namespace internal
}  // namespace candor
