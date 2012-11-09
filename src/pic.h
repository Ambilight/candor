#ifndef _SRC_PIC_H_
#define _SRC_PIC_H_

#include <unistd.h> // intptr_t

namespace candor {
namespace internal {

class CodeSpace;

class PIC {
 public:
  PIC(CodeSpace* space);

  char* Generate();

 protected:
  void Miss(char* object, char* property, intptr_t result);
  void Invalidate(char** ip);

  static const int kMaxSize = 10;

  CodeSpace* space_;
  char* protos_[kMaxSize];
  char* props_[kMaxSize];
  intptr_t results_[kMaxSize];
  int index_;
};

} // namespace internal
} // namespace candor

#endif // _SRC_PIC_H_