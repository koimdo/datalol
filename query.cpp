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
  template<size_t i, size_t size, typename F, typename... Ts>
  struct for_each {
    static constexpr
    bool
    run(F&& f, Ts const&... ts)
    {
      return f(i, std::get<i>(ts)...) && for_each<i+1, size, F, Ts...>::run(std::forward<F>(f), ts...);
    }
  };
  template<size_t size, typename F, typename... Ts>
  struct for_each<size, size, F, Ts...> {
    static constexpr
    bool
    run(F&& f, Ts const&... ts)
    {
      return true;
    }
  };
} // namespace detail

template<typename F, typename T0, typename... Ts>
bool
for_each_in_tuple(F&& f, const T0& t0, const Ts&... ts)
{
  static constexpr size_t arity = std::tuple_size<T0>::value;
  static_assert(std::conjunction<std::bool_constant<arity == std::tuple_size<Ts>::value>...>::value, "All tuples myust have the same arity");
  return detail::for_each<0, arity, F, T0, Ts...>::run(std::forward<F>(f), t0, ts...);
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

template<typename T>
struct print_tuple {
  const T& t;
  print_tuple(const T& t): t(t) {}
  friend std::ostream& operator<<(std::ostream& os, const print_tuple& p)
  {
    os << "<";
    auto intr = !for_each_in_tuple(generic_print{os}, p.t);
    if (intr)
      os << ", ...";
    return os << ">";
  }
};

struct Var_ {
  std::string name;
  Var_(const Var_&) = delete;
  Var_(const std::string& name): name(name) {}
  bool operator<(const Var_& o) const { return name < o.name; }

  mutable const void *p = nullptr; // FIXME: maybe std::optional<T> or similar. For now we have address persistence.
  void zap() const { p = nullptr; }
  bool is_unset() const { return !p; }
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
class DB;
struct Relation_base {
  DB& db;
  std::string name;
  Relation_base(DB& db, const std::string& name)
    : db(db)
    ,name(name)
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

  template<typename Sel, typename Row, size_t i, size_t size>
  struct check_query_t {
    using SElem = decltype(std::get<i>(*(const Sel*)nullptr));
    using RElem = decltype(std::get<i>(*(const Row*)nullptr));
    static constexpr bool check1 = check_arg<SElem, RElem>::value;
    static_assert(check1, "Type mismatch");
    static constexpr bool value = check1 && check_query_t<Sel, Row, i+1, size>::value;
  };

  template<typename Sel, typename Row, size_t size>
  struct check_query_t<Sel, Row, size, size> : std::bool_constant<true> {};

  struct unify1 {
    template<class R> constexpr bool operator()(int, const R& s, const R& r) const { return s == r; }
    template<class R> constexpr bool operator()(int, const Var<R>& s, const R& r) { return s.unify(r); }
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

  void print(std::ostream& os) const override { os << print_tuple(selector); }
  QueryFragment(Relation_base& rel, Selector&&... sels)
    : QueryFragment_base(rel)
    , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
  {}

  
  void eval1(const void *p) override
  {
    const value_type& row = *static_cast<const value_type*>(p);
    if (for_each_in_tuple(detail::unify1(), selector, row)) {
      next->eval();
    }
    bt.undo();
  }

  void eval() override
  {
    for_each_in_tuple(bt, selector); // Record unset vars

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
    return os << " }";
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

template<typename T>
struct Relation : Relation_base {
  friend class DB;
  using Relation_base::Relation_base;

  using value_type = T;
  flat::set<value_type> all;

  friend std::ostream& operator<<(std::ostream& os, const Relation& r)
  {
    os << "{";
    for (auto const& row : r.all)
      os << "\n  " << r.name << "(" << print_tuple(row) << ")";
    os <<"\n}";
    return os;
  }

  template<typename... Args>
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

class DB {
public:
  template<typename... Args>
  Relation<std::tuple<Args...>>
  table(const std::string& name)
  {
    static_assert(!std::disjunction<std::is_base_of<Var_, Args>...>::value, "Cannot have var type");
    return Relation<std::tuple<Args...>>(*this, name);
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
  DB db;
  auto R = db.table<int, int, int>("R");

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  R.insert(0, 2, 0);
  std::cout << R << "\n"; 

  auto S = db.table<std::string, int, std::string>("S");
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
