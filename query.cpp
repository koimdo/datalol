#include <iostream>
#include <sstream>
#include <set>
#include <array>
#include <tuple>

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

namespace detail
{
  template<typename T, typename F, int... Is>
  bool
  for_each(T&& t, F&& f, std::integer_sequence<int, Is...>)
  {
    bool cont = true;
    auto l = { ((cont = cont && f(Is, std::get<Is>(t))) , 0)... };
    return cont;
  }
} // namespace detail

template<typename... Ts, typename F>
bool
for_each_in_tuple(std::tuple<Ts...> const& t, F&& f)
{
  return detail::for_each(t, std::forward<F>(f), std::make_integer_sequence<int, sizeof...(Ts)>());
}

struct generic_print {
  std::ostream& os;
  template<typename T>
  bool operator () (int i, T const &v)
  {
    os << (i? ", " : "") << v;
    return true;
  }
};

template<typename... Args>
std::ostream& operator<<(std::ostream& os, const std::tuple<Args...>& t)
{
  os << "<";
  auto intr = !for_each_in_tuple(t, generic_print{os});
  return os << (intr ? "|" : ">");
}

struct Var_ {
  std::string name;
  Var_(const Var_&) = delete;
  Var_(const std::string& name): name(name) {}
  bool operator<(const Var_& o) const { return name < o.name; }

  mutable const void *p = nullptr; // FIXME: maybe std::optional<T> or similar. For now we have address persistence.
  void zap() const { p = nullptr; }
  bool is_unset() const { !p; }
};

template<class T>
struct Var : public Var_ {
  using Var_::Var_;

  bool unify(const T& t) const
  {
    if (p)
      return t == *get();
    p = &t;
    return true;
  }

  const T *get() const { return static_cast<const T*>(p); }
  friend std::ostream& operator<<(std::ostream& os, const Var& v)
  {
    os << "?" << v.name;
    if (v.p) {
      const T& t = *v.get();
      os << "=" << t;
    }
    return os;
  }
};

struct res_cb {
  virtual void eval() = 0;
  res_cb *next = nullptr;
  res_cb& operator>>(res_cb&& n)
  {
    next = &n;
    return *this;
  }
};

class QueryFragment_base;
struct Relation_base {
  std::string name;
  Relation_base(const std::string& name)
    : name(name)
  {}
  virtual void run(QueryFragment_base& q) = 0;
};

struct QueryFragment_base : res_cb {
  Relation_base& rel;
  virtual void eval1(const void *p) = 0; // FIXME: virtual call on the hot path
  virtual void print(std::ostream&) const = 0;
  QueryFragment_base(Relation_base& rel)
    : rel(rel)
  {}
  friend std::ostream& operator<<(std::ostream& os, const QueryFragment_base& qf)
  {
    os << qf.rel.name << "(";
    qf.print(os);
    return os << ")";
  }
};

namespace detail {
  template<class S, class R> struct check_arg : std::bool_constant<false> {};
  template<class R> struct check_arg<R,             R > : std::bool_constant<true> {};
  template<class R> struct check_arg<Var<R>&, const R&> : std::bool_constant<true> {};

  template<size_t i, class S, class R> struct check1 {
    static constexpr bool value = check_arg<S, R>::value;
    static_assert(value, "Type mismatch");
  };

  template<typename Sel, typename Row, size_t i, size_t size>
  struct check_query_t {
    using SElem = decltype(std::get<i>(*(const Sel*)nullptr));
    using RElem = decltype(std::get<i>(*(const Row*)nullptr));
    static constexpr bool value =
      check1<i, SElem, RElem>::value &&
      check_query_t<Sel, Row, i+1, size>::value;
  };

  template<typename Sel, typename Row, size_t size>
  struct check_query_t<Sel, Row, size, size> : std::bool_constant<true> {};
  
  template<class R> constexpr bool unify1(const R& s, const R& r) { return s == r; }
  template<class R> constexpr bool unify1(const Var<R>& s, const R& r) { return s.unify(r); }
  
  // This class unifies tuples elementwise
  template<typename Sel, typename Row, size_t i, size_t size>
  struct unify {
    static constexpr bool
    exec(const Sel& sel, const Row& row)
    {
      return
        unify1(std::get<i>(sel), std::get<i>(row)) &&
        unify<Sel, Row, i+1, size>::exec(sel, row);
      }
    };

  template<typename Sel, typename Row, size_t size>
  struct unify<Sel, Row, size, size> {
    static constexpr bool
    exec(const Sel&, const Row&) { return true; }
  };

  struct backtrack {
    std::vector<const Var_*> vars;
    
    template<typename T>
    bool operator()(int, Var<T>& v)
    {
      if (v.is_unset())
        vars.push_back(&v);
      return true;
    }

    template<typename T>
    bool operator()(int, const T&) { return true; }
    void undo() {
      for (auto v : vars)
        v->zap();
    }
  };
}

template<typename value_type, typename... Selector>
struct QueryFragment : QueryFragment_base {
  // TODO: value_type can be derived from selector (at least for value_type = tuple<...>)
  using query_type = std::tuple<Selector...>;
  static constexpr int arity = std::tuple_size<query_type>::value;
  static_assert(std::tuple_size<value_type>::value == arity, "Inconsistent lengths");
  static_assert(detail::check_query_t<query_type, value_type, 0, arity>::value, "Type mismatch");

  query_type selector;
  detail::backtrack bt;

  void print(std::ostream& os) const override { os << selector; }
  QueryFragment(Relation_base& rel, Selector&&... sels)
    : QueryFragment_base(rel)
    , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
  {}

  
  void eval1(const void *p) override
  {
    const value_type& row = *static_cast<const value_type*>(p);
    if (detail::unify<query_type, value_type, 0, std::tuple_size<query_type>::value>::exec(selector, row)) {
      next->eval();
    }
    bt.undo();
  }

  void eval() override
  {
    for_each_in_tuple(selector, bt); // Record unset vars

    rel.run(*this);
  }
};

struct Query : res_cb {
  std::vector<QueryFragment_base*> qfs;
  Query& append(QueryFragment_base& qf)
  {
    if (qfs.size())
      qfs.back()->next = &qf;
    qfs.emplace_back(&qf);
    return *this;
  }
  Query(QueryFragment_base&& qf) { append(qf); }
  Query operator&(QueryFragment_base&& qf) const
  {
    Query res(*this);
    return res.append(qf);
  }
  friend std::ostream& operator<<(std::ostream& os, const Query& q)
  {
    os << "{";
    for (auto const& qf : q.qfs)
      os << " " << *qf;
    os << " }";
  } 
  void configure_pipeline(res_cb *next) {
    assert(qfs.size());
    qfs.back()->next = next;
  }
  void eval() override {
    if (next)
      next->eval();
    else
      std::cout << *this << "\n";
  } // TODO: real results
};

Query operator&(QueryFragment_base&& l, QueryFragment_base&& r)
{
  return Query(std::move(l)) & std::move(r);
}

template<typename... Args>
struct Relation : Relation_base {
  static_assert(!std::disjunction<std::is_base_of<Var_, Args>...>::value ,  "Cannot have var type");
  Relation(const std::string& name)
    : Relation_base(name)
  {}

  using value_type = std::tuple<Args...>;
  flat::set<value_type> all;

  friend std::ostream& operator<<(std::ostream& os, const Relation& r)
  {
    os << "{";
    for (auto const& row : r.all)
      os << "\n  " << r.name << "(" << row << ")";
    os <<"\n}";
    return os;
  }

  void insert(Args&&... args) {
    all.emplace(std::forward<Args>(args)...);
  }



  template<typename... SelectArgs>
  QueryFragment<value_type, SelectArgs...>
  operator()(SelectArgs&&... args) {
    return QueryFragment<value_type, SelectArgs...>(*this, std::forward<SelectArgs>(args)...);
  }

  void run(QueryFragment_base& q) override {
    assert(this == &q.rel);
    for (auto const& row : all)
      q.eval1(&row);
  }
};

void select(Query&& qf)
{
  std::cout << "SELECT(" << qf << "):\n";
  qf.configure_pipeline(&qf);
  qf.qfs[0]->eval();
}

int main()
{
  Relation<int, int, int> R("R");

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  R.insert(0, 2, 0);
  std::cout << R << "\n"; 

  Relation<std::string, int, std::string> S("S");
  S.insert("Hello", 2, "Hello");
  S.insert("Hello", 2, "Datalog");
  S.insert("LOL", 1, "LOL");
  S.insert("Goodbye", 1, "Query");
  S.insert("Hello", 3, "World");
  std::cout << S << "\n"; 
  
  Var<int> x("x");
  Var<int> y("y");

  select(R(1, 2, 3));
  select(R(1, x, y));
  select(R(1, x, x));
  select(R(1, x, 0));
  select(R(x, x, 3));

  Var<std::string> s("s");
  select(S(s, y, s));

  select(R(1, y, 3) &
         S(s, y, s));


}
