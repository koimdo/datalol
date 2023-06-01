#include <iostream>
#include <sstream>
#include <set>
#include <array>

#include "flat/set"
#include "flat/span"

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

enum Kind {
  TVAR = 0,
  TINT,
  TFLOAT,
  TSTRING,
};

struct Var;
union Value {
  static_assert(sizeof(long) == sizeof(void*));
  static_assert(sizeof(double) == sizeof(void*));
  static_assert(alignof(long) == alignof(void*));
  static_assert(alignof(double) == alignof(void*));
  long i;
  double f;
  const std::string *s;
  const Var *v;
  Value(int i): i(i) {}
  Value(float f): f(f) {}
  Value(const std::string& s): s(&s) {}
  Value(const Var &v): v(&v) {}
  void format(std::ostream& os, Kind k) const;
  static bool eq(Value l, Value r, Kind k);
  static bool lt(Value l, Value r, Kind k);
};

template<class T>
struct kind_of {};

template<> struct kind_of<int> : std::integral_constant<Kind, TINT> {};
template<> struct kind_of<float> : std::integral_constant<Kind, TFLOAT> {};
template<> struct kind_of<std::string> : std::integral_constant<Kind, TSTRING> {};
template<> struct kind_of<const char*> : std::integral_constant<Kind, TSTRING> {};
template<size_t N> struct kind_of<const char[N]> : std::integral_constant<Kind, TSTRING> {};
template<> struct kind_of<Var> : std::integral_constant<Kind, TVAR> {};
  
struct EDB {
  std::set<std::string> intern;

  Value of(int i) { return Value(i); }
  Value of(float f) { return Value(f); }
  Value of(const char *s) { return of(std::string(s)); }
  Value of(const std::string& s)
  {
    auto itb = intern.emplace(s);
    return Value(*itb.first);
  }
  Value of(const Var& v) { return Value(v); }
};

struct Var {
  std::string name;
  Var(const std::string& name): name(name) {}
  bool operator<(const Var& o) const { return name < o.name; }

  mutable Kind kind = TVAR;
  mutable const Value *val = nullptr;
  bool unify(const Value *v) const
  {
    if (val)
      return Value::eq(*val, *v, kind);
    val = v;
    return true;
  }
  void zap() const { val = nullptr; }
  bool is_unset() const { return !val; }
};

void Value::format(std::ostream& os, Kind k) const
{
  switch (k) {
  case TINT: os << i; break;
  case TFLOAT: os << f; break;
  case TSTRING: os << "\"" << *s << "\""; break;
  case TVAR:
    os << "?" << v->name;
    if (v->val) {
      os << "=";
      v->val->format(os, v->kind);
    }
    break;
  }
}

bool Value::eq(Value l, Value r, Kind k) {
  switch (k) {
  case TINT: return l.i == r.i;
  case TFLOAT: return l.f == r.f;
  case TSTRING: return l.s == r.s; // Strings are interned!
  case TVAR:
    assert(false);
  }
}

bool Value::lt(Value l, Value r, Kind k) {
  switch (k) {
  case TINT: return l.i < r.i;
  case TFLOAT: return l.f < r.f;
  case TSTRING: return *l.s < *r.s;
  case TVAR:
    assert(false);
  }
}

template<class T>
class striderator {
  T *p;
  ptrdiff_t stride;
public:
  striderator(T *p, ptrdiff_t stride): p(p), stride(stride) {}
  constexpr bool operator!=(const striderator& o) const { return p != o.p; }
  striderator& operator++()
  {
    p += stride;
    return *this;
  }
  striderator& operator++(int)
  {
    striderator res = *this;
    ++(*this);
    return *res;
  }
  flat::span<T> operator*() { return flat::span<T>(p, p+stride); }
};

using row_t = flat::span<Value>;

struct res_cb {
  virtual void operator()() const = 0;
};

class Relation_base;
struct QueryFragment : res_cb {
  mutable const res_cb *next = nullptr; // FIXME
  Relation_base& rel;
  std::vector<Value> sel;
  std::vector<Kind> seltype;
  friend std::ostream& operator<<(std::ostream& os, const QueryFragment& qf);
  QueryFragment(Relation_base& rel, flat::span<Value> sel_, flat::span<Kind> seltype_)
    : rel(rel)
    , sel(sel_.begin(), sel_.end())
    , seltype(seltype_.begin(), seltype_.end())
  {}

  bool unify(row_t row) const;
  void operator()() const override { return eval(); }
  void eval() const;
};

struct Query : res_cb {
  std::vector<QueryFragment> qfs;
  Query(QueryFragment&& qf) { qfs.emplace_back(qf); }
  Query& operator+=(QueryFragment&& qf)
  {
    qfs.emplace_back(qf);
  }
  Query operator&(QueryFragment&& qf) const
  {
    Query res(*this);
    res += std::move(qf);
    return res;
  }
  friend std::ostream& operator<<(std::ostream& os, const Query& q)
  {
    os << "{";
    for (auto const& qf : q.qfs)
      os << " " << qf;
    os << " }";
  }
  void configure_pipeline(const res_cb *next) const {
    auto beg = qfs.rbegin();
    auto end = qfs.rend();
    for ( ; beg != end; ++beg) {
      beg->next = next;
      next = &(*beg);
    }
  }
  void operator()() const override { std::cout << *this << "\n"; } // TODO: real results
};

Query operator&(QueryFragment&& l, QueryFragment&& r)
{
  Query res(std::move(l));
  res.qfs.emplace_back(r);
  return res;
}

struct Relation_base {
  EDB& edb;
  std::string name;
  flat::span<Kind> dtype;
  Relation_base(EDB& edb, const std::string& name, flat::span<Kind> dtype)
    : edb(edb), name(name), dtype(dtype)
  {}

  static void format_tuple(std::ostream& os, row_t row, flat::span<Kind> dtype)
  {
    assert(row.size() == dtype.size());
    for (int i=0; i<dtype.size(); ++i) {
      if (i) os << ", ";
      row[i].format(os, dtype[i]);
    }
  }

  void format_tuple(std::ostream& os, row_t row) const { format_tuple(os, row, dtype); }

  using gen_iterator = striderator<const Value>;
  virtual gen_iterator begin() const = 0;
  virtual gen_iterator end() const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Relation_base& r)
  {
    os << "{";
    for (auto row : r) {
      os << "\n  " << r.name << "(";
      r.format_tuple(os, row);
      os  << ")";
    }
    os <<"\n}";
    return os;
  }
};


// unify([1, 2, 3], [1, 2, 3]) -> true
// unify([1, 2, 3], [1, x, y]) -> true
// unify([1, 2, 3], [1, x, 4]) -> false
// unify([1, 2, 3], [1, x, x]) -> false
// unify([1, 2, 2], [1, x, x]) -> true
bool QueryFragment::unify(row_t t) const
{
  assert(sel.size() == t.size());
  for (int i=0; i<sel.size(); i++) {
    Kind kind = seltype[i];
    if (TVAR == kind) {
      const Var *v = sel[i].v;
      assert(rel.dtype[i] == v->kind);
      if (!v->unify(&t[i])) {
        return false;
      }
    } else if (!Value::eq(sel[i], t[i], kind))
      return false;
  }
  return true;
}
void QueryFragment::eval() const
{
  std::vector<const Var*> vars;
  for (int i=0; i<sel.size(); ++i)
      if (seltype[i] == TVAR) {
        const Var *v = sel[i].v;
        if (v->is_unset())
          vars.push_back(v);
      }

  for (auto row : rel) {
    if (unify(row))
      (*next)();
    for (auto v : vars)
      v->zap();
  }
}

std::ostream& operator<<(std::ostream& os, const QueryFragment& qf)
{
  os << qf.rel.name << "(";
  Relation_base::format_tuple(os, qf.sel, qf.seltype);
  return os << ")";
}

template<typename... Args>
struct Relation : Relation_base {
  static std::array<Kind, sizeof...(Args)> type;
  Relation(EDB& edb, const std::string& name)
    : Relation_base(edb, name, type)
  {}

  static constexpr int arity = sizeof...(Args);
  using tuple_t = std::tuple<Args...>;
  using value_type = std::array<Value, arity>; // TODO: tuple it
  using query_type = std::array<Value, arity>;

  gen_iterator begin() const override { return gen_iterator(all.begin()->data(), arity); }
  gen_iterator end() const override { return gen_iterator(all.end()->data(), arity); }

  struct cmp_tuples {
    bool operator()(const value_type& ls, const value_type& rs) const
    {
      for (int i=0; i<arity; ++i) {
        auto const& l = ls[i];
        auto const& r = rs[i];
        auto t = type[i];
        if (!Value::eq(l, r, t))
          return Value::lt(l, r, t);
      }
      return false;
    }
  };

  flat::set<value_type, cmp_tuples> all;

  template<typename... TArgs>  
  std::pair<value_type, flat::span<Kind>>
  tuplify(bool allow_vars, TArgs&&... args)
  {
    static constexpr std::array<Kind, sizeof...(TArgs)> kinds{(kind_of<typename std::remove_reference<TArgs>::type>::value)...};
    value_type vals({edb.of(std::forward<TArgs>( args))...});

    assert(kinds.size() == type.size());
    if (allow_vars) {
      for (int i=0; i<type.size(); i++) {
        Kind k = kinds[i];
        Kind t = type[i];
        if (TVAR == k)
          vals[i].v->kind = t;
        else
          assert(k == t);
      }
    } else {
      assert(kinds == type);
    }

    return {std::move(vals), std::move(kinds)};
  }

  void insert(Args&&... args) {
    auto vk = tuplify(false, std::forward<Args>(args)...);
    all.emplace(std::move(vk.first));
  }

  template<typename... SelectArgs>
  QueryFragment
  operator()(SelectArgs&&... args) {
    static_assert(sizeof...(SelectArgs) == arity, "Wrong arity!");
    auto vk = tuplify(true, std::forward<SelectArgs>(args)...);
    return QueryFragment(*this, vk.first, vk.second);
  }
};
template<typename... Args>
std::array<Kind, sizeof...(Args)> Relation<Args...>::type{(kind_of<Args>::value)...};

void select(const Query& qf)
{
  std::cout << "SELECT(" << qf << "):\n";
  qf.configure_pipeline(&qf);
  qf.qfs[0].eval();
}

int main()
{
  EDB edb;
  Relation<int, int, int> R(edb, "R");

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  R.insert(0, 2, 0);
  std::cout << R << "\n"; 

  Relation<std::string, int, std::string> S(edb, "S");
  S.insert("Hello", 2, "Hello");
  S.insert("Hello", 3, "World");
  std::cout << S << "\n"; 
  
  Var x("x");
  Var y("y");
  Var z("z");


  select(R(1, y, 3) &
         S(x, y, z));

  select(R(1, 2, 3));
  select(R(1, x, y));
  select(R(1, x, x));
  select(R(1, x, 0));
  select(R(x, x, 3));

  select(S(x, y, x));
  select(R(x, y, x));
}
