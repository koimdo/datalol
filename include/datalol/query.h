#pragma once

#include "syntax.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <functional>

#include <flat/set>
#include <flat/map>

class cow_buf {
  static constexpr size_t MAX_SIZE = 1024;
  const void *p = nullptr;
  void (*destroy)(const void *) = nullptr;

  // Assumption: the held data is never aligned wider than std::align_t
  alignas(std::max_align_t) unsigned char buf[MAX_SIZE];

public:
  constexpr explicit operator bool() const noexcept { return p; }

  ~cow_buf();
  void clear();

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

class Var_ : public IPrint {
protected:
  struct Impl {
    std::string name;
    mutable cow_buf p;
  };
  flat::pool_ptr<Impl> impl;

public:
  Var_(const Var_&) = default;
  Var_(const std::string& name);
  bool operator<(const Var_& o) const { return impl->name < o.impl->name; }
  void zap() const { impl->p.clear(); }
  bool is_unset() const { return !impl->p; }
  const std::string& get_name() const noexcept { return impl->name; }
};

template<class T>
class Var : public Var_ {
public:
  using Var_::Var_;

  bool unify(const T& t) const
  {
    if (impl->p)
      return *get() == t;
    impl->p.assign(t);
    return true;
  }

  bool unify(T&& t) const
  {
    if (impl->p)
      return *get() == t;
    impl->p.assign(std::move(t));
    return true;
  }

  const T *get() const { return static_cast<const T*>(impl->p.get()); }
  const T *operator->() const { return get(); }
  const T& operator*() const { return *get(); }
  void print(std::ostream& os) const override
  {
    os << "?" << impl->name;
    if (impl->p) {
      const T& t = *get();
      os << "=" << t;
    }
  }
};

struct query_fragment : Rule::Body {
  query_fragment *next = nullptr;
};

class Collection_base;
template<class T> class Relation;
template<class T> class Objects;

class DB {
  template<class T> friend class Relation;
  template<class T> friend class Objects;
  flat::autorelease pool;
  flat::map<std::string, flat::pool_ptr<Collection_base>> rels;

  template<class Rel>
  Rel& make_relation(const std::string& name)
  {
    auto rel = pool.allocate<Rel>(*this, name);
    auto itb = rels.emplace(name, rel);
    assert(itb.second);
    return *rel;
  }

public:
  DB(): pool("db") {}
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

class Collection_base : public IPrint {
protected:
  DB& db;
  std::string name;
public:
  Collection_base(const Collection_base&) = delete;
  Collection_base(DB& db, const std::string& name)
    : db(db)
    , name(name)
  {}
  const std::string& get_name() const noexcept { return name; }
  virtual size_t merge() = 0;
};

namespace detail {
  template<class S, class R> struct check_arg   : bool_constant<false> {};
  template<class R> struct check_arg<R,      R> : bool_constant<true> {};
  template<class R> struct check_arg<Var<R>, R> : bool_constant<true> {};

  template<typename Sel, typename Row, size_t i, size_t size>
  struct check_query_t {
    using SElem = typename std::tuple_element<i, Sel>::type;
    using RElem = typename std::tuple_element<i, Row>::type;
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

  struct get_value {
    template<typename T>
    const T& operator()(const Var<T>& v) const { return *v.get(); }
    template<typename T>
    constexpr const T& operator()(const T& t) const { return t; }
  };
}

template<typename value_type>
struct Match_base : query_fragment {
  // TODO: any positive content for Match_base. perhaps list of bound vars?
};

#define DATALOL(...) DQuery([&]() { __VA_ARGS__ ; })
class DQuery : Query, query_fragment {
  void eval_body(Rule& r, size_t) override;
  void print(std::ostream& os) const override;
  struct cmp {
    bool operator()(Collection_base *l, Collection_base *r) const;
  };
  flat::set<Collection_base *, cmp> to_merge; // TODO: real query plan
  void configure();

public:
  template<typename F>
  DQuery(F&& build)
    : Query(std::move(build))
  {
    configure();
  }

  void run();
};

// TODO: despecialize relations?

template<typename T>
struct Relation : Collection_base {
  friend class DB;
  using Collection_base::Collection_base;

  using value_type = T;
  flat::set<value_type> all;
  flat::set<value_type> delta, next_delta;
  static constexpr int arity = std::tuple_size<T>::value;
  template<size_t N>
  static
  bool index_cmp(const value_type& l, const value_type& r)
  {
    return
      std::get<N>(l) < std::get<N>(r) ||
      std::get<N>(l) == std::get<N>(r) && l < r;
  }

  typedef flat::set<value_type, bool (*)(const value_type& l, const value_type& r)> index_t;
  template<size_t... Is>
  static constexpr std::array<index_t, arity> make_indices(std::index_sequence<Is...>)
  {
    return { index_t(&index_cmp<Is>)... };
  }
  std::array<index_t, arity> indices = make_indices(std::make_index_sequence<arity>());

  size_t merge() override
  {
    std::cerr << "Next delta " << name << " : " << next_delta.size() << "\n";
    std::cerr << "Merging " << name << " delta: ";
    print_(std::cerr, delta);
    std::cerr <<"\n";
    if (all.empty()) {
      std::swap(all, delta);
    } else if (!delta.empty()) {
      all = all.set_union(delta);
    }
    delta = next_delta.diff(all);
    next_delta.clear();
    return delta.size();
  }

  void print(std::ostream& os) const override
  {
    print_(os, all);
    for (int i=0; i<arity; i++) {
      os << "\nIndex " << i << ": ";
      print_(os, indices[i]);
    }
  }

  template<class S>
  void print_(std::ostream& os, const S& s) const
  {
    os << "{";
    for (auto const& row : s)
      os << "\n  " << name << "(" << print_tuple<value_type>(row) << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    T it(std::forward<Args>(args)...);
    all.insert(it);
    for (int i=0; i<arity; i++)
      indices[i].insert(it);
  }

  template<typename... Selector>
  struct Match : Match_base<value_type>, public Rule::Head {
    Match(const Match&) = default;
    using query_type = std::tuple<Selector...>;
    static constexpr int arity = std::tuple_size<query_type>::value;
    static_assert(std::tuple_size<value_type>::value == arity, "Inconsistent lengths");
    static_assert(detail::check_query_t<query_type, value_type, 0, arity>::value, "Type mismatch");

    Relation<value_type>& rel;
    query_type selector;

    void print(std::ostream& os) const override
    {
      os << rel.name << "(" << print_tuple<query_type>(selector) << ")";
    }

    Collection_base *collection() { return &rel; }

    Match(Relation<value_type>& rel, Selector&&... sels)
      : rel(rel)
      , selector(std::forward<Selector>(sels)...) // FIXME: don't copy vars!
    {}
    void eval_body(Rule& r, size_t idx) override
    {
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, selector); // Record unset vars
      for (auto const& row : idx == r.seminaive_current ? this->rel.delta : this->rel.all) {
        if (for_each_in_tuple(detail::unify1(), selector, row))
          this->next->eval_body(r, idx+1);
        bt.undo();
      }
    }
    void eval_head(Rule& r) override
    {
      auto res = transform_each(selector, detail::get_value{});
      rel.next_delta.insert(std::move(res));
    }
  };

  template<typename... SelectArgs>
  flat::pool_ptr<Match<typename flat::remove_cvref<SelectArgs>::type...>>
  operator()(SelectArgs&&... args) {
    return flat::allocate<Match<typename flat::remove_cvref<SelectArgs>::type...>>(*this, std::forward<typename flat::remove_cvref<SelectArgs>::type>(args)...);
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
  flat::set<flat::pool_ptr<T>> all;

  size_t merge() override {
    return 0;
  }

  void print(std::ostream& os) const override
  {
    os << "{";
    for (auto const& row : all)
      os << "\n  " << name << "(" << *row << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    all.insert(db.pool.allocate<value_type>(std::forward<Args>(args)...));
  }

  void insert(flat::pool_ptr<T> p) {
    all.insert(p);
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
    void eval_body(Rule& r, size_t idx) override
    {
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, selector); // Record unset vars
      for (auto const& urow : this->rel.all) {
        const value_type& row = *urow;
        if (for_each_in_tuple(apply_sels(row), selector)) {
          this->last = &row;
          this->next->eval_body(r, idx+1);
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



#define HEAD_WITH(expr,...) flat::allocate<head>(([=,##__VA_ARGS__]() -> void { (void)(expr); }), #expr)
struct head : Rule::Head {
  using fun_t = std::function<void()>;
  fun_t f;
  std::string desc;
  head(fun_t&& f, const std::string& desc = "<head>");
  void eval_head(Rule&) override;
  void print(std::ostream& os) const override;
};

#define GUARD(expr,...) flat::allocate<guard>(([=,##__VA_ARGS__]() -> bool { return (expr); }), #expr)
struct guard : query_fragment {
  using fun_t = std::function<bool()>;
  fun_t f;
  std::string desc;
  guard(fun_t&& f, const std::string& desc = "<guard>");
  void eval_body(Rule& r, size_t idx) override;
  void print(std::ostream& os) const override;
};
