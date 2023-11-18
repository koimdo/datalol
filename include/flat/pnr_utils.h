#pragma once

#define rdassert(cond, message) assert(cond && message)

#include <algorithm>
#include <vector>
#include <iostream>
#include <fstream>
#include <cctype>
#include <sstream>
#include <string>
#include <climits>
#include <cstring>
#include <flat/set>
#include <flat/map>

struct unit { // like `void`, but with a (single) value
  constexpr bool operator==(unit) const { return true; }
  constexpr bool operator<(unit) const { return false; }
};


class format {
  std::ostringstream s;
public:
  template<class T>
  format& operator<<(const T& t) { s << t; return *this; }
  // stream manipulators. not caught by the above template overload.
  format& operator<<(std::ios_base& (*func)(std::ios_base&)) { s << func; return *this; }
  format& operator<<(std::ios_base& (*func)(std::ios&))      { s << func; return *this; }
  format& operator<<(std::ios_base& (*func)(std::ostream&))  { s << func; return *this; }
  operator std::string() const { return s.str(); }
};

// If you see a user of this function, it is because they are trying to delete an item from
// a container while iterating it with a range-based loop, which is never a good idea,
// even for containers that do not invalidate other itrators on erase().  In conclusion,
// this is a band-aid, and the code needs to be fixed (use iterator-based loop and some
// care).
template<typename Coll, typename Res = std::vector<typename Coll::value_type, typename Coll::allocator_type>>
Res copy_of(const Coll& coll) { return Res(coll.begin(), coll.end()); }

template <typename T, typename K>
bool contains(const T &setOrMap, const K& key) {
  return setOrMap.count(key);
}

/******************************************************************************
 * Tracing and debugging infrastructure
 *
 * pnr_trace enables passing trace options and debug options to P&R without requiring recompilation.
 * It also enables tracing P&R that was compiled for package install (i.e. without debug symbols).
 *
 * Recommended usage example:
 *
 * #define TRACE_CONTEXT "DDG_VIEW"
 * TRACE(REGION_BALANCER)
 * #undef REGION_BALANCER
 *
 * //...
 *
 * if (TRACE_REGION_BALANCER) {
 *    // dotit, visualizeit, debug prints, etc.
 * }
 *
 * TRACE_REGION_BALANCER << "Some debug output\n";
 *
 * will output the message:
 * "DDG_VIEW.REGION_BALANCER[0]: Some debug output"
 *
 * to standard error
 *
 * Activation:
 * PMA_PNR_TRACE=DDG_VIEW.REGION_BALANCER pmaopt kernel.pir
 *
 * Some interesting capabilities:
 * - Staged trace messages:
 *   Can define a context by managing increasing a stage number for a trace object:
 *
 *   ++TRACE_REGION_BALANCER;
 *   TRACE_REGION_BALANCER << "stage 1 messages\n";
 *
 *   ++TRACE_REGION_BALANCER << "a stage 2 message\n";
 *
 *   will output:
 *   DDG_VIEW.REGION_BALANCER[1]: stage 1 messages
 *   DDG_VIEW.REGION_BALANCER[2]: a stage 2 message
 *
 * - Can query the stage
 *   Useful for naming .dot files (see Trace::dot_filename)
 *
 * - Can have code conditional on trace (e.g. dot):
 *   if (TRACE_BALANCE_REGIONS) {
 *      std::string filename = Trace::dot_filename("balance_regions_",
 *          TRACE_BALANCE_REGIONS.get_stage());
 *      ddg.dotit(filename);
 *   }
 *
 * - Filter messages by stage:
 *   PMA_PNR_TRACE="DDG_VIEW.REGION_BALANCER:{2,4}"
 *
 *   Will only print messsages dropped to TRACE_REGION_BALANCER in stages 2 and 4
 *   The rest will go quietly into the night
 *
 * - Dependent messages:
 *   PMA_PNR_TRACE="DDG_VIEW.REGION_BALANCER:{2},TC.TRAVEL_COMPANION:{DDG_VIEW.REGION_BALANCER}"
 *   Will output messages sent to TRACE_REGION_BALANCER only in stage 2
 *   Will output messages sent to TRAVEL_COMPANION only when TRACE_REGION_BALANCER is in stage 2
 *
 * - Dependent grouped messages:
 *   PMA_PNR_TRACE="DDG_VIEW.REGION_BALANCER:{2},TC:{DDG_VIEW.REGION_BALANCER}"
 *   Will output messages sent to TRACE_REGION_BALANCER only in stage 2
 *   Will output any messages sent to trace optinos in the "TC" context only when TRACE_REGION_BALANCER is in stage 2
 *
 * Caveats:
 *   - Recursion is currently not handled (i.e. bi-dependence).
 *     Will probably add something to deal with it in the future.
 *   - The parser is very weak. It works, but may break if you haven't
 *     specified the string correctly.
 *
 * See more usage examples in pnr_utils.cpp unit test
 ******************************************************************************/
template <typename T>
class from_string {
  const char *s;
  std::size_t *pos;
public:
  from_string(const char *s, std::size_t *pos = 0) : s(s), pos(pos) {}

  T convert(const char *s, std::size_t *pos = 0) const {
    rdassert(0 , "Unimplemetned type");
    return T();
  }
  operator const T() {
    return convert(s, pos);
  }

  bool operator==(const T& val) const {
    return convert(s, pos) == val;
  }
};

inline std::string str_convert(const char *s, std::size_t *pos) {
   std::size_t p=0;
   std::string res;
   /* skip whtiespace */
   while (s[p] == ' ' || s[p] == '\t') p++;
   while (std::isalnum(s[p]) || s[p] == '.' || s[p] == '_' || s[p] == '.' || s[p] == '-')
     res.push_back(s[p++]);
   if (pos)
     *pos = p;
   return res;
}

#define FROM_STRING(type, converter) \
template<> \
inline type from_string<type>::convert(const char *s, std::size_t *pos) const {\
    try { \
      std::size_t p=(pos)? *pos : 0; \
      type res = converter(s+p, &p);\
      if (pos) (*pos)+=p;\
      return res;\
    } catch (std::invalid_argument) {\
      return (type)0;\
    }\
} \
\
template <> \
inline std::vector<type> from_string<std::vector<type>>::convert(const char *s, std::size_t *pos) const { \
  std::size_t p=0; \
  if (!pos) pos = &p; \
  std::vector<type> result; \
  /* skip white spaces */\
  while (s[*pos] == ' ' || s[*pos] == '\t') (*pos)++; \
  if (s[(*pos)++] != '[') return result; \
  do { \
    result.push_back(from_string<type>(s, pos)); \
  } while (s[(*pos)++] == ','); \
  return result; \
} \
\
template <> \
inline flat::set<type> from_string<flat::set<type>>::convert(const char *s, std::size_t *pos) const { \
  std::size_t p=0; \
  if (!pos) pos = &p; \
  flat::set<type> result; \
  /* skip white spaces */\
  while (s[*pos] == ' ' || s[*pos] == '\t') (*pos)++; \
  if (s[(*pos)++] != '{') return result; \
  do { \
    result.insert(from_string<type>(s, pos)); \
  } while (s[(*pos)++] == ','); \
  return result; \
}

FROM_STRING(int, std::stoi);
FROM_STRING(long, std::stol);
FROM_STRING(unsigned long, std::stoul);
FROM_STRING(long long, std::stoll);
FROM_STRING(unsigned long long, std::stoull);
FROM_STRING(float, std::stof);
FROM_STRING(double, std::stod);
FROM_STRING(long double, std::stold);
//FROM_STRING(std::string, str_convert);
#undef FROM_STRING

namespace std {
template <typename T>
inline string to_string(const std::vector<T>& vec) {
  std::string output = "[";
  for (int i=0; i < vec.size()-1; i++) {
    output += to_string(vec.at(i)) + ", ";
  }
  output += std::to_string(vec.at(vec.size()-1));
  output += "]";
  return output;
}
}

// I'm pretty much fed up with the __INLIB__ madness and initialization order fiasco.
struct Initialize {
  class Notify {
    friend class Initialize;
    Initialize *head;           // We definitely don't initialize any members here, since
                                // any initialization will destroy the list built so far.
    bool ready;                 // For global static instances, we are in the bss anyway so zero-initialized
  public:
    Notify();
    void done();           // To be called after the producer (e.g. pnr_trace ) is initialized
  };
protected:
  void init();
  Initialize(Notify& notify);
  Initialize(std::nullptr_t) {}
private:
  Notify *owner;
  Initialize *next;
  virtual void init_() = 0;
  Initialize(const Initialize&) = delete;
};

class TraceOption;
class Trace : public Initialize::Notify {
  const char *key;
  
  flat::map<std::string, std::string> key_value_pairs;
  flat::map<std::string, std::pair<TraceOption*, int>> trace_options;
  void parse(const char *value);
public:
  Trace(const char *key) : key(key) {
    const char *value = std::getenv(key);
    if (value)
      parse(value);
    done();
  }
  bool operator()(const std::string& subkey) const {
    if (!key_value_pairs.size())
      return false;
    return key_value_pairs.find(subkey) != key_value_pairs.end();
  }

  int *register_trace_option(const std::string& key, TraceOption *option) {
    auto itb = trace_options.emplace(key, std::make_pair(option, 0));
    return &(itb.first->second.second);
  }

  TraceOption *find_option(const std::string& key) {
    auto it = trace_options.find(key);
    if (it == trace_options.end())
      return NULL;
    return it->second.first;
  }

  // for testing purposes
  void reload() {
    key_value_pairs.clear();
    const char *value = std::getenv(key);
    if (value)
      parse(value);
    done();
  }

  template <typename T>
  T get_value(const std::string subkey) {
    if (!key_value_pairs.size())
      return T();
    auto it = key_value_pairs.find(subkey);
    if (it == key_value_pairs.end())
      return T();
    return from_string<T>(it->second.c_str());
  }

  /*
   * A convenience tracing utility for generating .dot file names associated with counters
   */
  static std::string dot_filename(const std::string prefix, int count, const std::string suffix=".dot") {
    return prefix + std::to_string(count) + suffix;
  }
};

extern Trace pnr_trace;

class TraceOption : private Initialize, private std::streambuf { // cache result, enable hierarchy, complex values, etc.
  bool enabled = false;
  bool start_line = true;
  std::string msg_prefix;
  int *stage=0;
  flat::set<int> stages;
  mutable std::vector<std::string> initial_trace_options;
  mutable std::vector<TraceOption*> trace_options;
  std::streambuf *out;
  std::ostream os;

  void collect_trace_options(const std::string& key) {
    // examine the value as string to see if it is a set or a single value
    for (auto val : pnr_trace.get_value<flat::set<std::string>>(key)) {
      if (val.size()>0 && std::isalpha(val.at(0))) {
        initial_trace_options.push_back(val);
      } else {
        stages.insert(from_string<int>(val.c_str()));
      }
    }
  }
  void init_() override;
  friend class Trace;
  static bool all_enabled;
public:
  TraceOption(const std::string& key);

  bool dependent_option() const {
    for (auto *opt : trace_options) {
      if (opt->is_enabled())
        return true;
    }
    return false;
  }

  int get_stage() const { return *stage; }

  bool is_enabled() const {
    if (!enabled)
      return all_enabled;
    if (initial_trace_options.size() > 0) {
      for (auto key : initial_trace_options) {
        auto *option = pnr_trace.find_option(key);
        if (option) // ignore unrgistered options
          trace_options.push_back(option);
      }
      initial_trace_options.clear();
    }
    bool dependencies = stages.size() > 0 || trace_options.size() > 0;
    return (!dependencies ||
        contains(stages, get_stage()) ||
        dependent_option());
  }

  int_type overflow(int_type c) override {
    char buf[256]; // 256 bytes should be enough (prefix) for anyone
    if (!is_enabled())
      return std::char_traits<char>::not_eof(c);
    switch (c) {
    case EOF:
      return std::char_traits<char>::not_eof(c);
    case '\n':
      start_line = true;
      return out->sputc('\n');
    default:
      rdassert(c >= 0 && c <= UCHAR_MAX, "");
      if (start_line) {
        int len = snprintf(buf, sizeof(buf),
                           "%.*s[%d]: ",
                           (int)msg_prefix.size(), msg_prefix.data(), get_stage());
        if (out->sputn(buf, len) != len)
          return EOF;
      }
      start_line = false;
      return out->sputc(c);
    }
  }

  explicit operator bool() const { return is_enabled(); }

  template <typename T>
  const T& operator()(const char *msg, const T& val) {
    (*this) << msg << " (" << std::to_string(val) << ")\n";
    return val;
  }

  bool operator()(const char *msg, bool cond) {
    (*this) << msg << " (" << ((cond) ? "true" : "false") << ")\n";
    return cond;
  }

  template<class T>
  TraceOption& operator<<(const T& t) {
    if (is_enabled())
      os << t;
    return *this;
  }

  // The attentive reader may wonder why I chose to add methods that are substantially
  // similar to the above (templated) operator<<.
  // As it turns out, having the positive fragment of Prolog with guards at your disposal
  // is not sufficient to resolve template overloaded functions with templated overloaded
  // functions as arguments. SAD.
  TraceOption& operator<<(std::ios_base& (*func)(std::ios_base&)) {
    if (is_enabled())
      os << func;
    return *this;
  }

  TraceOption& operator<<(std::ios& (*func)(std::ios&)) {
    if (is_enabled())
      os << func;
    return *this;
  }

  TraceOption& operator<<(std::ostream& (*func)(std::ostream&)) {
    if (is_enabled())
      os << func;
    return *this;
  }

  TraceOption& operator++() {
    (*stage)++;
    return *this;
  }
};

template <typename T>
class DefaultValueOption : public Initialize {
  const char *key;
  T default_value;
public:
  DefaultValueOption(const char *key, const T& default_value)
    : Initialize(pnr_trace)
    , key(key), default_value(default_value)
  {
    init();
  }

  void init_() override {
    if (pnr_trace(key))
      default_value = pnr_trace.get_value<T>(key);
  }

  operator const T&() {
    return default_value;
  }

  friend
  bool operator==(const T& t, const DefaultValueOption& o) { return o.default_value == t; }

  friend
  bool operator==(const DefaultValueOption& o, const T& t) { return o.default_value == t; }
};

#define __STRINGIFY(x) #x
// Note: requires defining a TRACE_CONTEXT (usually capitalized filename, e.g. DDG_VIEW)
#define TRACE(key) \
  static TraceOption TRACE_##key(TRACE_CONTEXT  "."  __STRINGIFY(key));

#define DEFAULT(key, type, value) \
  static DefaultValueOption<type> DEFAULT_##key("DEFAULT_" __STRINGIFY(key), value);

static DefaultValueOption<int> PNR_DOT_VERBOSITY("PNR_DOT_VERBOSITY", 0);

struct Assert {
  Assert(bool cond=true, const char *msg = "") {
    rdassert(cond && msg, "");
  }
};

template <typename Key>
struct Compare {
  bool operator()(const Key& lhs, const Key& rhs) const {
    return lhs < rhs;
  }
};
template <typename Keyp>
struct Compare<Keyp*> {
  bool operator()(const Keyp *lhs, const Keyp *rhs) const {
    return *lhs < *rhs;
  }
};


