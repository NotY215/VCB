#include "vcb.hpp"

namespace vcb {

const TargetInfo SysV_x86_64 = {
  "x86_64-sysv",
  6,
  { "rdi", "rsi", "rdx", "rcx", "r8", "r9" },
  "rax",
  16
};

} // namespace vcb