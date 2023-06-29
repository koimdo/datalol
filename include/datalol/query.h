#pragma once

#include "syntax.h"

#include <map>
#include <cstddef>
#include <datalol/tuple_util.h>
#include <functional>

#include <flat/set>

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
  mutable cow_buf p;
  std::string name;

public:
  Var_(const Var_&) = delete;
  Var_(const std::string& name): name(name) {}
  bool operator<(const Var_& o) const { return name < o.name; }
  void zap() const { p.clear(); }
  bool is_unset() const { return !p; }
};

template<class T>
class Var : public Var_ {
public:
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

struct DQuery : Query, query_fragment {
  using Query::Query;
  void configure();
  void run();
  void eval_body(Rule& r) override;
  void print(std::ostream& os) const override;
};

// TODO: despecialize relations?

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
    void eval_head(Rule& r) override
    {
      auto res = transform_each(selector, detail::get_value{});
      this->rel.all.insert(std::move(res));
    }
  };

  template<typename... SelectArgs>
  std::unique_ptr<Match<SelectArgs...>>
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
  head(fun_t&& f, const std::string& desc = "<head>");
  void eval_head(Rule&) override;
  void print(std::ostream& os) const override;
};

#define GUARD(expr) std::make_unique<guard>(([&]() -> bool { return (expr); }), #expr)
struct guard : query_fragment {
  using fun_t = std::function<bool()>;
  fun_t f;
  std::string desc;
  guard(fun_t&& f, const std::string& desc = "<guard>");
  void eval_body(Rule& r) override;
  void print(std::ostream& os) const override;
};
