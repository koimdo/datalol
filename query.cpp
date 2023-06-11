#include <iostream>
#include <set>
#include <tuple>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include <cstddef>

#include "flat/set"
#include "flat/span"

#include "syntax.h"
#include "tuple_util.h"

struct cow_buf {
  static constexpr size_t MAX_SIZE = 1024;
  const void *p = nullptr;
  void (*destroy)(const void *) = nullptr;

  // Assumption: the held data is never aligned wider than std::align_t
  alignas(std::max_align_t) unsigned char buf[MAX_SIZE];

  constexpr bool owned() const noexcept { return p == buf; }

  constexpr explicit operator bool() const noexcept { return p; }

  ~cow_buf() { clear();}
  void clear()
  {
    if (!p)
      return;
    if (destroy)
      (destroy)(p);

    p = nullptr;
    destroy = nullptr;
  }

  template<class T>
  void assign(const T& t)
  {
    clear();
    p = &t;
  }

  template<class T>
  void assign(T&& t)
  {
    // FIXME: wider alignment?
    clear();
    ::new (buf) T(std::forward<T>(t));
    p = buf;

    if (!std::is_trivially_destructible<T>::value)
      destroy = [](const void *p) { static_cast<const T*>(p)->~T(); };
  }

  constexpr const void *get() const noexcept { return p; }
};

struct Var_ : IPrint {
  std::string name;
  Var_(const Var_&) = delete;
  Var_(const std::string& name): name(name) {}
  bool operator<(const Var_& o) const { return name < o.name; }

  mutable cow_buf p;
  void zap() const { p.clear(); }
  bool is_unset() const { return !p; }
};

template<class T>
struct Var : public Var_ {
  using Var_::Var_;

  bool unify(const T& t) const
  {
    if (p)
      return *get() == t;
    p.assign(t);
    return true;
  }

  bool unify(T&& t) const
  {
    if (p)
      return *get() == t;
    p.assign(std::move(t));
    return true;
  }

  const T *get() const { return static_cast<const T*>(p.get()); }
  const T *operator->() const { return get(); }
  const T& operator*() const { return *get(); }
  void print(std::ostream& os) const override
  {
    os << "?" << name;
    if (p) {
      const T& t = *get();
      os << "=" << t;
    }
  }
};

struct query_fragment : Rule::Body {
  query_fragment *next = nullptr;
};

class DB;
struct Collection_base : public IPrint {
  Collection_base(const Collection_base&) = delete;
  DB& db;
  std::string name;
  Collection_base(DB& db, const std::string& name)
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
    using SElem = decltype(std::get<i>(std::declval<const Sel&>()));
    using RElem = decltype(std::get<i>(std::declval<const Row&>()));
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

  template<class T> struct get_var { static const Var_* get(const T&) { return nullptr; } };
  template<class T> struct get_var<Var<T>> { static const Var_* get(const Var<T>& v) { return &v; } };

  struct backtrack {
    const Var_ **vars;
    int nvars = 0;

    backtrack(const Var_ **vars): vars(vars) {}

    template<typename T>
    bool operator()(int, const T& t) {
      const Var_ *v = get_var<T>::get(t);
      if (v && v->is_unset())
        vars[nvars++] = v;
      return true;
    }
    void undo() {
      for (int i=0; i<nvars; i++)
        vars[i]->zap();
    }
  };
}

template<typename value_type>
struct Match_base : query_fragment {
  // TODO: any positive content for Match_base. perhaps list of bound vars?
};

struct DQuery : Query, query_fragment {
  using Query::Query;
  void configure()
  {
    for (auto& r : rules) {
      assert(!r.get_body().empty());
      assert(r.get_head());
      query_fragment *next = this;
      auto body = r.get_body();
      for (int i=body.size()-1; i >= 0; i--) {
        query_fragment *elem = static_cast<query_fragment*>(body[i]);
        elem->next = next;
        next = elem;
      }
    }
  }
  void run() {
    for (auto& r : rules) {
      r.get_body()[0]->eval_body(r);
    }
  }

  void eval_body(Rule& r) override {
    r.get_head()->eval_head(r);
  }

  void print(std::ostream& os) const override { Query::print(os); }
};

template<typename T>
struct Relation : Collection_base {
  friend class DB;
  using Collection_base::Collection_base;

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

  template<typename... Selector>
  struct Match : Match_base<value_type> {
    using query_type = std::tuple<Selector...>;
    static constexpr int arity = std::tuple_size<query_type>::value;
    static_assert(std::tuple_size<value_type>::value == arity, "Inconsistent lengths");
    static_assert(detail::check_query_t<query_type, value_type, 0, arity>::value, "Type mismatch");

    Relation<value_type>& rel;
    query_type selector;

    void print(std::ostream& os) const override { os << print_tuple<query_type>(selector); }
    Match(Relation<value_type>& rel, Selector&&... sels)
      : rel(rel)
      , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
    {}
    void eval_body(Rule& r) override
    {
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, selector); // Record unset vars
      for (auto const& row : this->rel.all) {
        if (for_each_in_tuple(detail::unify1(), selector, row))
          this->next->eval_body(r);
        bt.undo();
      }
    }
  };

  template<typename... SelectArgs>
  Rule::ubody
  operator()(SelectArgs&&... args) {
    return std::make_unique<Match<SelectArgs...>>(*this, std::forward<SelectArgs>(args)...);
  }
};

namespace detail {
  template<class T>
  struct match_elem {};

  template<class T, class M>
  struct data_base : match_elem<T> {
    M T::*m;
    data_base(M T::*m): m(m) {}
    const M& get(const T& t) const { return t.*m; }
  };

  template<class T, class M, class V>
  struct data_eq : data_base<T, M> {
    const V& v;
    data_eq(M T::*m, const V& v): data_base<T, M>(m), v(v) {}
    bool apply(const T& t) const { return this->get(t) == v; }
  };

  template<class T, class M>
  struct data_bind : data_base<T, M> {
    const Var<M>& v;
    data_bind(M T::*m, const Var<M>& v): data_base<T, M>(m), v(v) {}
    bool apply(const T& t) const { return v.unify(this->get(t)); }
  };

  template<class T, class M>
  struct get_var<data_bind<T, M>> { static const Var_* get(const data_bind<T, M>& d) { return &d.v; } };

  template<class T, class M>
  struct match_data_factory {
    M T::* m;
    constexpr match_data_factory(M T::*m): m(m) {}

    data_bind<T, M> operator==(const Var<M>& v) { return data_bind<T, M>(m, v); }

    template<class V>
    data_eq<T, M, V> operator==(const V& v) { return data_eq<T, M, V>(m, v); }
  };

}
template<typename T>
struct Objects : Collection_base {
  friend class DB;
  using Collection_base::Collection_base;

  using value_type = T;
  class cmp {
    bool operator()(const std::unique_ptr<T>& l, const std::unique_ptr<T>& r)
    {
      return *l < *r;
    }
  };
  flat::set<std::unique_ptr<value_type>> all;    // FIXME: node_set

  void print(std::ostream& os) const override
  {
    os << "{";
    for (auto const& row : all)
      os << "\n  " << name << "(" << *row << ")"; // FIXME: node_set
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    all.emplace(std::make_unique<value_type>(std::forward<Args>(args)...));
  }

  template<class M>
  detail::match_data_factory<T, M> operator[](M T::*p) { return p; }

  struct apply_sels {
    apply_sels(const value_type& t): t(t) {}
    const value_type& t;
    template<class S>
    bool operator()(int, const S& sel) { return sel.apply(t); }
  };

  template<typename... Selector>
  struct Match : Match_base<value_type> {
    using query_type = std::tuple<Selector...>;
    static_assert(detail::all<std::is_base_of<detail::match_elem<T>, Selector>::value...>::value, "Proper selectors");

    Objects<value_type>& rel;
    query_type selector;

    // TODO: move to printing or something
    std::array<const Var_*, sizeof...(Selector)> vars = {0};
    int nvars = 0;
    const value_type *last = nullptr;

    void print(std::ostream& os) const override
    {
      if (last)
        os << "<" << *last << "> : ";
      os << "{";
      for (int i=0; i<nvars; i++)
        os << ", " << *vars[i];
      os << "}";
    }
    Match(Objects<value_type>& rel, Selector&&... sels)
      : rel(rel)
      , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
    {
      detail::backtrack bt(vars.data());
      for_each_in_tuple(bt, selector);
      nvars = bt.nvars;
    }
    void eval_body(Rule& r) override
    {
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, selector); // Record unset vars
      for (auto const& urow : this->rel.all) {
        const value_type& row = *urow;
        if (for_each_in_tuple(apply_sels(row), selector)) {
          this->last = &row;
          this->next->eval_body(r);
        }
        bt.undo();
      }
    }
  };

  template<typename... SelectArgs>
  Match<SelectArgs...>
  operator()(SelectArgs&&... args) {
    return Match<SelectArgs...>(*this, std::forward<SelectArgs>(args)...);
  }
};


class DB {
  std::map<std::string, std::unique_ptr<Collection_base>> rels;

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
  Objects<T>&
  objects(const std::string& name) {
    return make_relation<Objects<T>>(name);
  }
};


#define HEAD_WITH(expr) std::make_unique<head>(([&]() -> void { (void)(expr); }), #expr)
struct head : Rule::Head {
  using fun_t = std::function<void()>;
  fun_t f;
  std::string desc;
  head(fun_t&& f, const std::string& desc = "<head>"): f(f), desc(desc) {}
  void eval_head(Rule&) override { f(); }
  void print(std::ostream& os) const { os << "head(" << desc << ")"; }
};

#define GUARD(expr) std::make_unique<guard>(([&]() -> bool { return (expr); }), #expr)
struct guard : query_fragment {
  using fun_t = std::function<bool()>;
  fun_t f;
  std::string desc;
  guard(fun_t&& f, const std::string& desc = "<guard>"): f(f), desc(desc) {}
  void eval_body(Rule& r) override
  {
    // TODO: bind vars in guards?
    if (f()) next->eval_body(r);
  }
  void print(std::ostream& os) const { os << "guard(" << desc << ")"; }
};

void select(DQuery&& qf)
{
  std::cout << "SELECT(" << (const Query&)qf << "):\n";
  qf.configure();
  qf.run();
}

struct A {
  int i, j, k;
  A(int i, int j, int k): i(i), j(j), k(k) {}
  A(int i, int j): A(i, j, i*j) {}
  bool lolz(int ofs) const { return k >= std::max(i, j + ofs); }
  int get_i() const { return i; }
  const int& get_j() const { return j; }
  friend std::ostream& operator<<(std::ostream& os, const A& a) {
    return os << "A{i=" << a.i << ", j=" << a.j << ", k=" << a.k << "}";
  }
  bool operator<(const A& o) const
  {
    return std::make_tuple(i, j, k) < std::make_tuple(o.i, o.j, o.k);
  }
};

struct raw_result : Rule::Head {
  void eval_head(Rule& r) override
  {
    std::cout << r << "\n";
  }
  void print(std::ostream& os) const override
  {
    os << "<debug print>";
  }
};

static
Rule::uhead
print_raw() { return std::make_unique<raw_result>(); }

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

  select(DQuery{{print_raw() << R(1, 2, 3)}});
  select(DQuery{{print_raw() << R(1, x, y)}});
  select(DQuery{{print_raw() << R(1, x, x)}});
  select(DQuery{{print_raw() << R(1, x, 0)}});
  select(DQuery{{print_raw() << R(x, x, 3)}});

  Var<std::string> s("s");
  select(DQuery{{print_raw() << S(s, y, s)}});

  auto& As = db.objects<A>("AS");

  As.insert(0, 1, 2);
  As.insert(1, 2, 3);
  As.insert(1, 1, 3);
  As.insert(1, 8);
  std::cout << As << "\n";

  select(As(As[&A::i] == 1, As[&A::k] == 3, As[&A::j] == x));


  select(DQuery{{
        HEAD_WITH(std::cout << y << " " << s << "\n") <<
        R(1, y, 3) &
        S(s, y, s) &
        GUARD(s->size() > 3)
      }});
}
