#pragma once

#ifndef  INFO_ALIGNMENT
#if defined(__LP64__)
#define  INFO_ALIGNMENT  16
#else
#define  INFO_ALIGNMENT  8
#endif
#endif

struct alignas(INFO_ALIGNMENT) debug_info {
  const char *file;
  const char *function;
  const int line;
  unsigned flags;
  unsigned tripcount;
};

#define DEBUG_INFO()                                              \
  ({                                                              \
    static struct debug_info here                                 \
      __attribute__((__used__, __section__("info")))              \
      = { __FILE__, __PRETTY_FUNCTION__, __LINE__ };               \
    &here;                                                        \
  })

bool is_debug() noexcept;
