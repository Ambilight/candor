#include "stubs.h"
#include "macroassembler-x64.h"
#include "runtime.h"

namespace dotlang {

#define __ masm()->

BaseStub::BaseStub(Masm* masm, StubType type) : FFunction(masm),
                                                type_(type) {
}


void AllocateStub::Generate() {
  __ push(rbp);
  __ movq(rbp, rsp);
  __ push(rbx);

  // Arguments
  Operand size(rbp, 24);
  Operand tag(rbp, 16);

  Label runtime_allocate(masm()), done(masm());

  Heap* heap = masm()->heap();
  Immediate heapref(reinterpret_cast<uint64_t>(heap));
  Immediate top(reinterpret_cast<uint64_t>(heap->new_space()->top()));
  Immediate limit(reinterpret_cast<uint64_t>(heap->new_space()->limit()));

  Operand scratch_op(scratch, 0);

  // Get pointer to current page's top
  // (new_space()->top() is a pointer to space's property
  // which is a pointer to page's top pointer
  // that's why we are dereferencing it here twice
  __ movq(scratch, top);
  __ movq(scratch, scratch_op);
  __ movq(rax, scratch_op);
  __ movq(rbx, rax);

  // Add object size to the top
  __ addq(rbx, size);
  __ jmp(kCarry, &runtime_allocate);

  // Check if we exhausted buffer
  __ movq(scratch, limit);
  __ movq(scratch, scratch_op);
  __ cmp(rbx, scratch_op);
  __ jmp(kGt, &runtime_allocate);

  // Update top
  __ movq(scratch, top);
  __ movq(scratch, scratch_op);
  __ movq(scratch_op, rbx);

  __ jmp(&done);

  // Invoke runtime allocation stub (and probably GC)
  __ bind(&runtime_allocate);

  RuntimeAllocateCallback allocate = &RuntimeAllocate;

  __ movq(scratch, Immediate(*reinterpret_cast<uint64_t*>(&allocate)));
  {
    Masm::Align a(masm_);
    __ Pushad();

    // Two arguments: heap and size
    __ movq(rdi, heapref);
    __ movq(rsi, size);
    __ callq(scratch);
    __ Popad();
  }

  // Voila result and result_end are pointers
  __ bind(&done);

  // Set tag
  Operand qtag(rax, 0);
  __ movq(scratch, tag);
  __ movq(qtag, scratch);

  // Rax will hold resulting pointer

  __ pop(rbx);
  __ movq(rsp, rbp);
  __ pop(rbp);

  // tag + size
  __ ret(16);
}


void CoerceTypeStub::Generate() {
  __ push(rbp);
  __ movq(rbp, rsp);
  __ push(rbx);

  // Arguments
  Operand lhs(rbp, 16);
  Operand rhs(rbp, 24);

  Label done(masm()), not_number(masm());
  __ movq(rax, Immediate(0));

  // Get both values
  __ movq(rbx, lhs);
  __ movq(rax, rhs);

  // Check if their tags are equal (just return second in that case)
  Operand qtag_lhs(rbx, 0), qtag_rhs(rax, 0);
  __ movq(scratch, qtag_lhs);
  __ cmp(scratch, qtag_rhs);
  __ jmp(kEq, &done);

  // If left is number
  __ cmp(qtag_lhs, Immediate(Heap::kTagNumber));
  __ jmp(kNe, &not_number);

  {
    // TODO: Coerce right to number
    __ emitb(0xcc);
  }

  __ jmp(&done);

  __ bind(&not_number);

  {
    // TODO: Coerce right to string
    __ emitb(0xcc);
  }

  __ bind(&done);

  // Rax will hold resulting pointer

  __ pop(rbx);
  __ movq(rsp, rbp);
  __ pop(rbp);

  // lhs + rhs
  __ ret(16);
}

void PropertyLookupStub::Generate() {
  __ push(rbp);
  __ movq(rbp, rsp);
  __ push(rbx);

  __ pop(rbx);
  __ movq(rsp, rbp);
  __ pop(rbp);

  // lhs + rhs
  __ ret(16);
}

} // namespace dotlang
