#include <iostream>
#include <set>
#include <tuple>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include "flat/set"
#include "flat/span"

// FIXME: remove when we are assured we have C++14 (or higher) also in csp
#ifndef __cpp_lib_make_unique
namespace std {
template<typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) { return unique_ptr<T>(new T(forward<Args>(args)...)); }
}
#endif

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

  template< bool B >
  using bool_constant = std::integral_constant<bool, B>;

  template<bool...> struct all : bool_constant<true> {};
  template<bool B, bool... Rest>
  struct all<B, Rest...> : bool_constant<B && all<Rest...>::value> {};

  template<bool...> struct any : bool_constant<false> {};
  template<bool B, bool... Rest>
  struct any<B, Rest...> : bool_constant<B || any<Rest...>::value> {};
} // namespace detail

template<typename F, typename T0, typename... Ts>
bool
for_each_in_tuple(F&& f, const T0& t0, const Ts&... ts)
{
  static constexpr size_t arity = std::tuple_size<T0>::value;
  static_assert(detail::all<(arity == std::tuple_size<Ts>::value)...>::value, "All tuples must have the same arity");
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
  const T *operator->() const { return get(); }
  const T& operator*() const { return *get(); }
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

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  friend std::ostream& operator<<(std::ostream& os, const IPrint& p) { return p.print(os), os; }
};

struct query_fragment : IPrint {
  virtual void eval() = 0;
  query_fragment *next = nullptr;
};

class DB;
struct Relation_base : public IPrint {
  Relation_base(const Relation_base&) = delete;
  DB& db;
  std::string name;
  Relation_base(DB& db, const std::string& name)
    : db(db)
    , name(name)
  {}
};

namespace detail {
  template<class S, class R> struct check_arg           : bool_constant<false> {};
  template<class R> struct check_arg<R,             R > : bool_constant<true> {};
  template<class R> struct check_arg<Var<R>&, const R&> : bool_constant<true> {};

  template<typename Sel, typename Row, size_t i, size_t size>
  struct check_query_t {
    using SElem = decltype(std::get<i>(*(const Sel*)nullptr));
    using RElem = decltype(std::get<i>(*(const Row*)nullptr));
    static constexpr bool check1 = check_arg<SElem, RElem>::value;
    static_assert(check1, "Type mismatch");
    static constexpr bool value = check1 && check_query_t<Sel, Row, i+1, size>::value;
  };

  template<typename Sel, typename Row, size_t size>
  struct check_query_t<Sel, Row, size, size> : bool_constant<true> {};

  struct unify1 {
    template<class R> constexpr bool operator()(int, const R& s, const R& r) const { return s == r; }
    template<class R> constexpr bool operator()(int, const Var<R>& s, const R& r) { return s.unify(r); }
  };

  struct backtrack {
    const Var_ **vars;
    int nvars = 0;

    backtrack(const Var_ **vars): vars(vars) {}

    template<typename T>
    bool operator()(int, Var<T>& v)
    {
      if (v.is_unset())
        vars[nvars++] = &v;
      return true;
    }

    template<typename T>
    bool operator()(int, const T&) { return true; }
    void undo() {
      for (int i=0; i<nvars; i++)
        vars[i]->zap();
    }
  };
}

template<typename> struct Relation;
template<typename value_type>
struct Match_base : query_fragment {
  using relation_t = Relation<value_type>;
  relation_t& rel;
  Match_base(relation_t& rel)
    : rel(rel)
  {}
};

template<typename value_type, typename... Selector>
struct Match : Match_base<value_type> {
  using query_type = std::tuple<Selector...>;
  using Match_base<value_type>::relation_t;
  static constexpr int arity = std::tuple_size<query_type>::value;
  static_assert(std::tuple_size<value_type>::value == arity, "Inconsistent lengths");
  static_assert(detail::check_query_t<query_type, value_type, 0, arity>::value, "Type mismatch");

  query_type selector;

  void print(std::ostream& os) const override { os << print_tuple<query_type>(selector); }
  Match(Relation<value_type>& rel, Selector&&... sels)
    : Match_base<value_type>(rel)
    , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
  {}
  void eval() override;
};

struct Query : query_fragment {
  Query(const Query&) = delete;
  std::vector<query_fragment*> qfs;
  void append(const query_fragment& qf)
  {
    query_fragment *cqf = const_cast<query_fragment*>(&qf);
    if (qfs.size())
      qfs.back()->next = cqf;
    qfs.emplace_back(cqf);
  }
  Query(const query_fragment& qf) { append(qf); }

  void print(std::ostream& os) const override
  {
    os << "{";
    for (auto const& qf : qfs)
      os << " " << *qf;
    os << " }";
  }

  void run() {
    assert(qfs.size());
    qfs.back()->next = this;
    qfs[0]->eval();
  }

  void eval() override {
    assert(!next);
    std::cout << *this << "\n";
  }
};

Query&& operator&(Query&& l, const query_fragment& r) { return l.append(r), std::move(l); }

template<typename T>
struct Relation : Relation_base {
  friend class DB;
  using Relation_base::Relation_base;

  using value_type = T;
  flat::set<value_type> all;

  void print(std::ostream& os) const override
  {
    os << "{";
    for (auto const& row : all)
      os << "\n  " << name << "(" << print_tuple<value_type>(row) << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    all.emplace(std::forward<Args>(args)...);
  }

  template<typename... SelectArgs>
  Match<value_type, SelectArgs...>
  operator()(SelectArgs&&... args) {
    return Match<value_type, SelectArgs...>(*this, std::forward<SelectArgs>(args)...);
  }
};


template<typename value_type, typename... Selector>
void
Match<value_type, Selector...>::eval()
{
  const Var_ *vars[sizeof...(Selector)];
  detail::backtrack bt(vars);      // Cannot have more unset vars than query size
  for_each_in_tuple(bt, selector); // Record unset vars
  for (auto const& row : this->rel.all) {
    if (for_each_in_tuple(detail::unify1(), selector, row))
      this->next->eval();
    bt.undo();
  }
}

class DB {
  std::map<std::string, std::unique_ptr<Relation_base>> rels;

  template<class Rel>
  Rel& make_relation(const std::string& name)
  {
    auto rel = std::make_unique<Rel>(*this, name);
    auto p = rel.get();
    auto itb = rels.emplace(name, std::move(rel));
    assert(itb.second);
    return *p;
  }

public:
  template<typename... Args>
  Relation<std::tuple<Args...>>&
  table(const std::string& name)
  {
    static_assert(!detail::any<std::is_base_of<Var_, Args>::value...>::value, "Cannot have var type");
    return make_relation<Relation<std::tuple<Args...>>>(name);
  }

  template<typename T>
  Relation<T>&
  objects(const std::string& name) {
    return make_relation<Relation<T>>(name);
  }
};

struct tail : query_fragment {
  using fun_t = std::function<void()>;
  fun_t f;
  tail(fun_t&& f): f(f) {}
  void eval() { f(); }
  void print(std::ostream& os) const { os << "<tail>"; }
};

#define GUARD(expr) guard([&]() -> bool { return (expr); }, #expr)
struct guard : query_fragment {
  using fun_t = std::function<bool()>;
  fun_t f;
  std::string desc;
  guard(fun_t&& f, const std::string& desc = "<guard>"): f(f), desc(desc) {}
  void eval()
  {
    // TODO: bind vars in guards?
    if (f()) next->eval();
  }
  void print(std::ostream& os) const { os << "guard(" << desc << ")"; }
};

void select(Query&& qf)
{
  std::cout << "SELECT(" << qf << "):\n";
  qf.run();
}

int main()
{
  DB db;
  auto& R = db.table<int, int, int>("R");

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  R.insert(0, 2, 0);
  std::cout << R << "\n"; 

  auto& S = db.table<std::string, int, std::string>("S");
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
         S(s, y, s) &
         GUARD(s->size() > 3) &
         tail([&]() { std::cout << y << " " << s << "\n";}));


}
