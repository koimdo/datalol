#pragma once

namespace datalol {

namespace detail {

enum debug_flags {
  BREAK_CONFIGURE = 1<<0,
  BREAK_FIXPOINT  = 1<<1,
  BREAK_END       = 1<<2,
};

struct debug_info {
  const char *name;
  const char *file;
  const char *function;
  const int line;
  unsigned flags;
  unsigned tripcount;
};

#define DEBUG_INFO(name)                                                \
  ({                                                                    \
    static struct ::datalol::detail::debug_info dbg                     \
      = { #name, __FILE__, __PRETTY_FUNCTION__, __LINE__ };             \
    static struct ::datalol::detail::debug_info *here                                      \
      __attribute__((__used__, __section__("query_info")))              \
      = &dbg;                                                           \
    here;                                                               \
  })

#define DEBUG_PROBE(dflags) if (this->dbg->flags) debug_break(this->dbg, dflags)
void debug_break(const debug_info *dbg, debug_flags pos);

}

}

