#ifndef _SRC_X64_ASSEMBLER_INL_H_
#define _SRC_X64_ASSEMBLER_INL_H_

#include "assembler-x64.h"

#include <assert.h> // assert
#include <string.h> // memcpy, memset
#include <stdlib.h> // NULL

namespace dotlang {

inline void Label::relocate(char* pos) {
  // Label should be relocated only once
  assert(pos_ == NULL);
  pos_ = pos;

  // Iterate through all label's uses and insert correct relocation info
  List<char*, EmptyClass>::Item* item = addrs_.head();
  while (item != NULL) {
    emit(item->value());
    item = item->next();
  }
}


inline void Label::use(char* addr) {
  if (pos_ == NULL) {
    // If label wasn't allocated - add address into queue
    addrs_.Push(addr);
  } else {
    // Otherwise insert correct offset
    emit(addr);
  }
}


inline void Label::emit(char* addr) {
  int32_t* imm = reinterpret_cast<int32_t*>(addr);
  *imm = static_cast<int32_t>(reinterpret_cast<uint64_t>(pos_) -
         reinterpret_cast<uint64_t>(addr) -
         sizeof(int32_t));
}


inline void Assembler::emit_rex_if_high(Register src) {
  if (src.high() == 1) emitb(0x40 | 0x01);
}


inline void Assembler::emit_rexw(Register dst) {
  emitb(0x48 | dst.high() << 2);
}


inline void Assembler::emit_rexw(Operand& dst) {
  emitb(0x48 | dst.base().high() << 2);
}


inline void Assembler::emit_rexw(Register dst, Register src) {
  emitb(0x48 | dst.high() << 2 | src.high());
}


inline void Assembler::emit_rexw(Register dst, Operand& src) {
  emitb(0x48 | dst.high() << 2 | src.base().high());
}


inline void Assembler::emit_rexw(Operand& dst, Register src) {
  emitb(0x48 | dst.base().high() << 2 | src.high());
}


inline void Assembler::emit_modrm(Register dst) {
  emitb(0xC0 | dst.low() << 3);
}


inline void Assembler::emit_modrm(Operand &dst) {
  if (dst.scale() == Operand::one) {
    emitb(0x80 | dst.base().low());
    emitl(dst.disp());
  } else {
    // TODO: Support scales
  }
}


inline void Assembler::emit_modrm(Register dst, Register src) {
  emitb(0xC0 | dst.low() << 3 | src.low());
}


inline void Assembler::emit_modrm(Register dst, Operand& src) {
  if (src.scale() == Operand::one) {
    emitb(0x80 | dst.low() << 3 | src.base().low());
    emitl(src.disp());
  } else {
  }
}


inline void Assembler::emit_modrm(Register dst, uint32_t op) {
  emitb(0xC0 | op << 3 | dst.low());
}


inline void Assembler::emitb(uint8_t v) {
  *reinterpret_cast<uint8_t*>(pos()) = v;
  offset_ += 1;
  Grow();
}


inline void Assembler::emitw(uint16_t v) {
  *reinterpret_cast<uint16_t*>(pos()) = v;
  offset_ += 2;
  Grow();
}


inline void Assembler::emitl(uint32_t v) {
  *reinterpret_cast<uint32_t*>(pos()) = v;
  offset_ += 4;
  Grow();
}


inline void Assembler::emitq(uint64_t v) {
  *reinterpret_cast<uint64_t*>(pos()) = v;
  offset_ += 8;
  Grow();
}


inline void Assembler::Grow() {
  if (offset_ + 32 < length_) return;

  char* new_buffer = new char[length_ << 1];
  memcpy(new_buffer, buffer_, length_);
  memset(new_buffer + length_, 0xCC, length_);

  delete[] buffer_;
  buffer_ = new_buffer;
  length_ <<= 1;
}

} // namespace dotlang

#endif // _SRC_X64_ASSEMBLER_INL_H_
