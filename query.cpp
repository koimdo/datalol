#include <iostream>
#include <sstream>
#include <set>

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
  static bool cmp(Value l, Value r, Kind k);
  bool operator<(const Value& o) const { return i < o.i; } // FIXME: remove once we get typed relations
};

template<class T>
struct kind_of {};

template<> struct kind_of<int> : std::integral_constant<Kind, TINT> {};
template<> struct kind_of<float> : std::integral_constant<Kind, TFLOAT> {};
template<> struct kind_of<std::string> : std::integral_constant<Kind, TSTRING> {};
template<> struct kind_of<const char*> : std::integral_constant<Kind, TSTRING> {};
template<size_t N> struct kind_of<const char(&)[N]> : std::integral_constant<Kind, TSTRING> {};
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
  Kind kind;
  std::string name;
  Var(Kind kind, const std::string& name): kind(kind), name(name) {}
  bool operator<(const Var& o) const { return name < o.name; }

  mutable const Value *val = nullptr;
  bool unify(const Value *v) const
  {
    if (val)
      return Value::cmp(*val, *v, kind);
    val = v;
    return true;
  }
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

bool Value::cmp(Value l, Value r, Kind k) {
  switch (k) {
  case TINT: return l.i == r.i;
  case TFLOAT: return l.f == r.f;
  case TSTRING: return l.s == r.s; // Strings are interned!
  case TVAR:
    assert(false);
  }
}

struct Tuple {
  Tuple(std::initializer_list<Value> l): vals(l) {}
  std::vector<Value> vals;
  void format(std::ostream& os, flat::span<Kind> kind) const
  {
    int i=0;
    for (auto const& v : vals) {
      if (i) os << ", ";
      v.format(os, kind[i]);
      i++;
    }
  }
  bool operator<(const Tuple& o) const { return vals < o.vals; }
};

struct Relation {
  EDB& edb;
  std::string name;
  std::vector<Kind> type;
  Relation(EDB& edb, const std::string& name, const std::vector<Kind>& type): edb(edb), name(name), type(type) {}

  flat::set<Tuple> all;
  friend std::ostream& operator<<(std::ostream& os, const Relation& r)
  {
    os << "{";
    for (auto const& t : r.all) {
      os << "\n  " << r.name << "(";
      t.format(os, r.type);
      os  << ")";
    }
    os <<"\n}";
    return os;
  }



  // unify([1, 2, 3], [1, 2, 3]) -> true
  // unify([1, 2, 3], [1, x, y]) -> true
  // unify([1, 2, 3], [1, x, 4]) -> false
  
  // unify([1, 2, 3], [1, x, x]) -> false
  bool unify(const Tuple& t, const Tuple& sel, flat::span<Kind> selkind) const {
    assert(t.vals.size() == sel.vals.size());
    for (int i=0; i<t.vals.size(); i++) {
      Kind kind = selkind[i];
      if (TVAR == kind) {
        const Var *v = sel.vals[i].v;
        if (!v->unify(&t.vals[i])) {
          return false;
        }
      } else if (!Value::cmp(sel.vals[i], t.vals[i], kind))
        return false;
    }
    return true;
  }

  bool check_type(flat::span<Kind> kinds, bool allow_var) const
  {
    assert(kinds.size() == type.size());
    for (int i=0; i<type.size(); i++) {
      if (type[i] != kinds[i] && !(allow_var && kinds[i] == TVAR))
        return false;
    }
    return true;
  }

  template<typename... Args>
  std::pair<Tuple, std::vector<Kind>>
  tuplify(bool allow_vars, Args... args)
  {
    std::vector<Kind> kinds({(kind_of<Args>::value)...});
    assert(check_type(kinds, allow_vars));
    Tuple vals({edb.of(args)...});
    return {std::move(vals), std::move(kinds)};
  }

  template<typename... Args>
  void insert(Args... args) {
    auto vk = tuplify(false, args...);
    all.emplace(std::move(vk.first));
  }

  // int select(const Tuple& sel)
  // {
  //   int res = 0;
  //   std::cout << name << ".select(" << sel << "): ";
  //   for (auto const& t : all) {
  //     if (t.unify(sel)) {
  //       std::cout << sel << "\n";
  //       res++;
  //     }
  //   }
  //   std::cout << res << "\n";
  //   return res;
  // }
};

int main()
{
  EDB edb;
  Relation R(edb, "R", {TINT, TINT, TINT});

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  std::cout << R << "\n"; 

  Relation S(edb, "S", {TSTRING, TINT, TSTRING});
  S.insert("Hello", 2, "Hello");
  std::cout << S << "\n"; 
  
  Var x(TINT, "x");
  Var y(TINT, "y");

  auto vk = R.tuplify(true, 1, x, x);
  vk.first.format(std::cout, vk.second);

  // R.select(Tuple({&i[1], &i[2], &i[3]}));
  // R.select(Tuple({&i[1], &x, &y}));
  // R.select(Tuple({&i[1], &x, &i[0]}));
  // R.select(Tuple({&x, &x, &i[3]}));
}
