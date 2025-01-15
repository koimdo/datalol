#pragma once

#include <json/json.h>

#ifndef  INFO_ALIGNMENT
#if defined(__LP64__)
#define  INFO_ALIGNMENT  16
#else
#define  INFO_ALIGNMENT  8
#endif
#endif

enum debug_flags {
  BREAK_CONFIGURE = 1<<0,
  BREAK_FIXPOINT  = 1<<1,
  BREAK_END       = 1<<2,
};

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

#define DEBUG_PROBE(dflags) if (this->dbg->flags) debug_break(this->dbg, dflags)

namespace datalol {
// Utility functions for JsonCpp arrays, in the spirit of Qt's QList:
Json::Value& operator<<(Json::Value& arr, Json::Value&& item);
Json::Value& operator<<(Json::Value& arr, const Json::Value& item);
Json::Value operator<<(Json::Value&& arr, Json::Value&& item);
Json::Value operator<<(Json::Value&& arr, const Json::Value& item);
void debug_break(const debug_info *dbg, debug_flags pos);
}
