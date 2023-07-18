#pragma once

#include "syntax.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <functional>

#include <flat/set>
#include <flat/map>

template<class T>
class Var : public Var_ {
public:
  using Var_::Var_;

  void assign(const T& t)
  {
    assert(!impl->p);
    impl->p.assign(t);
  }

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
  friend std::ostream& operator<<(std::ostream& os, const Var& v)
  {
    os << *v.impl;
    if (v.impl->p) {
      const T& t = *v.get();
      os << "=" << t;
    }
    return os;
  }
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

#define DATALOL(...) DQuery([&]() { __VA_ARGS__ ; })
class DQuery : Query {
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
      (std::get<N>(l) < std::get<N>(r)) ||
      (std::get<N>(l) == std::get<N>(r) && l < r);
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
  struct Match : public Rule::Head {
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

    Collection_base *collection() override { return &rel; }

    Match(Relation<value_type>& rel, Selector&&... sels)
      : Head(eval_head, eval_body)
      , rel(rel)
      , selector(std::forward<Selector>(sels)...)
    {}
    static void eval_body(Rule::Elem& self_, Rule& r, size_t idx)
    {
      Match& self = static_cast<Match&>(self_);
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, self.selector); // Record unset vars
      for (auto const& row : idx == r.seminaive_current ? self.rel.delta : self.rel.all) {
        if (for_each_in_tuple(detail::unify1(), self.selector, row))
          self.next->eval(r, idx+1);
        bt.undo();
      }
    }
    static void eval_head(Rule::Elem& self_, Rule& r, size_t)
    {
      Match& self = static_cast<Match&>(self_);
      auto res = transform_each(self.selector, detail::get_value{});
      self.rel.next_delta.insert(std::move(res));
    }
  };

  template<typename... SelectArgs>
  flat::pool_ptr<Match<typename flat::remove_cvref<SelectArgs>::type...>>
  operator()(SelectArgs&&... args) {
    return flat::allocate<Match<typename flat::remove_cvref<SelectArgs>::type...>>(*this, std::forward<typename flat::remove_cvref<SelectArgs>::type>(args)...);
  }
};

namespace detail {
  struct match_elem {};

  template<class Getter>
  struct match_base {
    Getter getter;
    Rule::vars_t vars;
    const char *desc;
    using prop_t = typename flat::remove_cvref<decltype(std::declval<Getter>()())>::type;
    match_base(const Rule::vars_t& vars, Getter&& getter_, const char *desc)
      : vars(vars), getter(std::move(getter_)), desc(desc)
    {}
  };

  template<class Getter>
  struct match : match_base<Getter>, match_elem {
    using typename match_base<Getter>::prop_t;
    Var<prop_t> prop;
    match(match_base<Getter>&& base, Var<prop_t>& prop)
      : match_base<Getter>(std::move(base))
      , prop(std::move(prop))
    {}

    bool apply() const
    {
      return this->prop.unify(this->getter());
    }
    match(const match&) = delete;
    match(match&&) = default;
  };

  template<class Getter> struct get_var<match<Getter>> { static const Var_* get(const match<Getter>& m) { return &m.prop; } };

  template<class Getter>
  std::ostream& operator<<(std::ostream& os, const match<Getter>& m)
  {
    os << "[";
    Query::print_vars(os, m.vars);
    os << "]";
    return os << m.desc << " -> " << m.prop;
  }

  template<typename Getter>
  match<Getter> operator==(match_base<Getter>&& g, Var<typename match_base<Getter>::prop_t>& v)
  {
    return match<Getter>(std::move(g), v);
  }

  template<typename Getter>
  match<Getter> operator==(Var<typename match_base<Getter>::prop_t>& v, match_base<Getter>&& g)
  {
    return match<Getter>(std::move(g), v);
  }

  struct apply_sel {
    template<class S>
    bool operator()(int, const S& sel) { return sel.apply(); }
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

  template<typename... Selector>
  struct Match : Rule::Elem {   // TODO: upgrade to Rule::Head
    using query_type = std::tuple<Selector...>;
    static_assert(detail::all<std::is_base_of<detail::match_elem, Selector>::value...>::value, "Proper selectors");

    Objects<value_type>& rel;
    Var<value_type> that;
    query_type selector;

    void print(std::ostream& os) const override
    {
      os << "<" << that << "> : " << print_tuple<query_type>(selector);
    }

    Match(Objects<value_type>& rel, Var<value_type>& that, Selector&&... sels)
      : Rule::Elem(eval_body)
      , rel(rel)
      , that(std::move(that))
      , selector(std::forward<Selector>(sels)...)
    {
      std::cerr << "Obj selector size = " << sizeof(selector) << "\n";
      // TODO: verify only `that` is referenced in selectors
    }

    static void eval_body(Rule::Elem& self_, Rule& r, size_t idx)
    {
      Match& self = static_cast<Match&>(self_);
      const Var_ *vars[sizeof...(Selector)];
      detail::backtrack bt(vars);      // Cannot have more unset vars than query size
      for_each_in_tuple(bt, self.selector); // Record unset vars
      for (auto const& urow : self.rel.all) {
        self.that.assign(*urow);
        if (for_each_in_tuple(detail::apply_sel{}, self.selector)) {
          self.next->eval(r, idx+1);
        }
        self.that.zap();
        bt.undo();
      }
    }
  };

  template<typename... SelectArgs>
  flat::pool_ptr<Match<typename flat::remove_cvref<SelectArgs>::type...>>
  operator()(Var<value_type>& that, SelectArgs&&... args) {
    return flat::allocate<Match<typename flat::remove_cvref<SelectArgs>::type...>>(*this, that, std::forward<typename flat::remove_cvref<SelectArgs>::type>(args)...);
  }
};

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define UNIQ(label) CONCAT(label, CONCAT(__, __LINE__))

#define CAPTURE_COMMON()                                  \
  Rule::vars_t UNIQ(vars);                                \
  flat::guard UNIQ(current_guard) =                       \
    Query::with_vars(&UNIQ(vars))

#define HEAD_WITH(expr,...) ({                                          \
      CAPTURE_COMMON();                                                 \
      flat::allocate<head>(([=,##__VA_ARGS__]() -> void { (void)(expr); }), #expr); \
    })

struct head : Rule::Head {
  using fun_t = std::function<void()>;
  fun_t f;
  std::string desc;
  head(fun_t&& f, const std::string& desc = "<head>");
  static void eval_head(Rule::Elem&, Rule&, size_t);
  void print(std::ostream& os) const override;
};

#define GUARD(expr,...) ({                                              \
      CAPTURE_COMMON();                                                 \
      flat::allocate<guard>(([=,##__VA_ARGS__]() -> bool { return (expr); }), #expr); \
    })

struct guard : Rule::Elem {
  using fun_t = std::function<bool()>;
  fun_t f;
  std::string desc;
  guard(fun_t&& f, const std::string& desc = "<guard>");
  static void eval_body(Rule::Elem& self_, Rule& r, size_t idx);
  void print(std::ostream& os) const override;
};

#define $_(expr,...) ({                                                 \
      CAPTURE_COMMON();                                                 \
      auto UNIQ(extract) = ([=,##__VA_ARGS__]() { return (expr); });    \
      detail::match_base<decltype(UNIQ(extract))>(UNIQ(vars), std::move(UNIQ(extract)), #expr); \
    })

