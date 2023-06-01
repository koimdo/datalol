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
  T& operator*() { return *p; }
};

struct Relation_base {
protected:
  EDB& edb;
  std::string name;
  flat::span<Kind> dtype;
  Relation_base(EDB& edb, const std::string& name, flat::span<Kind> dtype)
    : edb(edb), name(name), dtype(dtype)
  {}

  static void format_tuple(std::ostream& os, const Value *base, flat::span<Kind> dtype)
  {
    for (int i=0; i<dtype.size(); ++i) {
      if (i) os << ", ";
      base[i].format(os, dtype[i]);
    }
  }

  void format_tuple(std::ostream& os, const Value *base) const { format_tuple(os, base, dtype); }

  using gen_iterator = striderator<const Value>;
  virtual int get_arity() const = 0;
  virtual gen_iterator begin() const = 0;
  virtual gen_iterator end() const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Relation_base& r)
  {
    os << "{";
    for (auto const& t : r) {
      os << "\n  " << r.name << "(";
      r.format_tuple(os, &t);
      os  << ")";
    }
    os <<"\n}";
    return os;
  }

  // unify([1, 2, 3], [1, 2, 3]) -> true
  // unify([1, 2, 3], [1, x, y]) -> true
  // unify([1, 2, 3], [1, x, 4]) -> false
  // unify([1, 2, 3], [1, x, x]) -> false
  // unify([1, 2, 2], [1, x, x]) -> true
  bool unify(const Value* t, const Value* sel, flat::span<Kind> selkind) const {
    int size = get_arity();
    for (int i=0; i<size; i++) {
      Kind kind = selkind[i];
      if (TVAR == kind) {
        const Var *v = sel[i].v;
        assert(dtype[i] == v->kind);
        if (!v->unify(&t[i])) {
          return false;
        }
      } else if (!Value::eq(sel[i], t[i], kind))
        return false;
    }
    return true;
  }

  void select_(const Value* sel, flat::span<Kind> kinds)
  {
    std::vector<const Var*> vars;
    for (int i=0; i<kinds.size(); ++i)
      if (kinds[i] == TVAR)
        vars.push_back(sel[i].v);

    int res = 0;
    std::cout << name << ".select(";
    format_tuple(std::cout, sel, kinds);
    std::cout << "):\n";
    for (auto const& t : *this) {
      if (unify(&t, sel, kinds)) {
        format_tuple(std::cout, sel, kinds);
        std::cout << "\n";
        res++;
      }
      for (auto v : vars)
        v->zap();
    }
    std::cout << res << "\n";
  }
};

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

  int get_arity() const override { return arity; }
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

  bool unify(const value_type& t, const query_type& sel, flat::span<Kind> selkind) {
    return Relation_base::unify(t.data(), sel.data(), selkind);
  }

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
  void select(SelectArgs&&... args) {
    auto vk = tuplify(true, std::forward<SelectArgs>(args)...);
    Relation_base::select_(vk.first.data(), vk.second);
  }
};
template<typename... Args>
std::array<Kind, sizeof...(Args)> Relation<Args...>::type{(kind_of<Args>::value)...};

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

  R.select(1, 2, 3);
  R.select(1, x, y);
  R.select(1, x, x);
  R.select(1, x, 0);
  R.select(x, x, 3);

  S.select(x, y, x);
  R.select(x, y, x);
}
