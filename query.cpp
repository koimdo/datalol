#include <iostream>
#include <sstream>
#include "set"

using namespace flat;

enum Kind {
  TVAR = 0,
  TINT,
  TFLOAT,
  TSTRING,
};

struct Value {
  virtual std::string label() const = 0;
  virtual Kind kind() const = 0;
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

struct Int : Value {
  int i;
  Int(int i): i(i) {}
  std::string label() const override { return std::to_string(i); }
  Kind kind() const override { return TINT; }
};

struct String : Value {
  std::string s;
  std::string label() const override { return format{} << "\"" << s << "\""; }
  Kind kind() const override { return TSTRING; }
};

struct Var : Value {
  
  std::string name;
  Var(const std::string& name): name(name) {}
  Kind kind() const override { return TVAR; }
  std::string label() const override
  {
    format f;
    f << "?" << name;
    if (val)
      f << "=" << val->label();
    return f;
  }
  bool operator<(const Var& o) const { return name < o.name; }

  mutable unsigned epoch = 0;
  mutable const Value *val = nullptr;
};

struct Tuple {
  Tuple(std::initializer_list<const Value*> l): vals(l) {}
  std::vector<const Value*> vals;
  friend std::ostream& operator<<(std::ostream& os, const Tuple& t)
  {
    int i=0;
    for (auto v : t.vals) {
      if (i++) os << ", ";
      os << v->label();
    }
    return os;
  }

  // unify([1, 2, 3], [1, 2, 3]) -> true
  // unify([1, 2, 3], [1, x, y]) -> true
  // unify([1, 2, 3], [1, x, 4]) -> false
  
  // unify([1, 2, 3], [1, x, x]) -> false
  bool unify(const Tuple& sel) const {
    static unsigned epoch = 0;

    epoch++;
    assert(vals.size() == sel.vals.size());
    for (int i=0; i<vals.size(); i++) {
      const Value *l = vals[i];
      const Value *rv = sel.vals[i];
      if (l == rv)              // FIXME: compare?
        continue;

      const Var *v = dynamic_cast<const Var*>(rv);
      if (!v)
        return false;

      if (v->epoch != epoch) {
        v->epoch = epoch;
        v->val = l;
      } else if (v->val != l) {
        return false;
      }
    }
    return true;
  }
  bool operator<(const Tuple& o) const { return vals < o.vals; }
};

struct Relation {
  Relation(const std::string& name, const std::vector<Kind>& type): name(name), type(type) {}
  std::string name;
  std::vector<Kind> type;

  flat::set<Tuple> all;
  friend std::ostream& operator<<(std::ostream& os, const Relation& r)
  {
    os << "{";
    for (auto const& t : r.all)
      os << "\n  " << r.name << "(" << t << ")";
    os <<"\n}";
    return os;
  }


  void insert(Tuple&& t) { all.emplace(t); }
  int select(const Tuple& sel)
  {
    int res = 0;
    std::cout << name << ".select(" << sel << "): ";
    for (auto const& t : all) {
      if (t.unify(sel)) {
        std::cout << sel << "\n";
        res++;
      }
    }
    std::cout << res << "\n";
    return res;
  }
};

int main()
{
  Int i[] = {Int(0), Int(1), Int(2), Int(3)};

  Relation R("R", {TINT, TINT, TINT});
  R.insert(Tuple({&i[1], &i[2], &i[3]}));
  R.insert(Tuple({&i[1], &i[2], &i[3]}));
  R.insert(Tuple({&i[1], &i[1], &i[3]}));
  std::cout << R << "\n"; 

  Var x("x");
  Var y("y");

  R.select(Tuple({&i[1], &i[2], &i[3]}));
  R.select(Tuple({&i[1], &x, &y}));
  R.select(Tuple({&i[1], &x, &i[0]}));
  R.select(Tuple({&x, &x, &i[3]}));
}
