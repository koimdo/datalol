#pragma once

#include "syntax.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <array>

#include <flat/set>
#include <flat/map>


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

  struct mark_vars_ {
    Rule::vars_t res;
    template<typename T>
    bool operator()(int, const T& t)
    {
      if (const Var_ *v = get_var<T>::get(t))
        res.set(v->get_id());
      return true;
    }
  };
  template<typename... Selector>
  Rule::vars_t mark_vars(const std::tuple<Selector...>& sels)
  {
    mark_vars_ mv;
    for_each_in_tuple(mv, sels);
    return mv.res;
  }

  struct undo_helper {
    static_stack<Var_, Rule::MAX_VARS> st;
    void add_undo_(Var_* v) {
      if (v) st.emplace_back(*v);
    }
    void undo() {
      for (auto v : st)
        v.zap();
    }
  };

  struct get_value {
    template<typename T>
    const T& operator()(const Var<T>& v) const { return *v.get(); }
    template<typename T>
    constexpr const T& operator()(const T& t) const { return t; }
  };

  struct generic_print {
    std::ostream& os;
    template<typename T>
    bool operator () (int i, T const &v)
    {
      os << (i? ", " : "") << v;
      return true;
    }
    template<typename T>
    bool operator () (int i, const Var<T>& v)
    {
      os << (i? ", " : "");
      Var<T>::do_print(os, v);
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
}

#define DATALOL(...) Query([&]() { __VA_ARGS__ ; })

template<typename T, typename Compare = std::less<T>>
struct Typed_collection : Collection_base {
  using Collection_base::Collection_base;

  flat::set<T, Compare> all;
  flat::set<T, Compare> delta, next_delta;

  size_t merge() override final
  {
    std::cerr << "Next delta " << name << " : " << next_delta.size() << "\n";
    std::cerr << "Merging " << name << " delta: ";
    //print_(std::cerr, delta);
    std::cerr <<"\n";
    if (all.empty()) {
      std::swap(all, delta);
    } else if (!delta.empty()) {
      all = all.set_union(delta);
    }
    delta = next_delta.diff(all);
    // TODO: combined union/diff operation
    // FIXME: indices
    next_delta.clear();
    return delta.size();
  }
};

// TODO: despecialize relations?

template<typename T>
struct Relation : Typed_collection<T> {
  friend class DB;
  using Typed_collection<T>::Typed_collection;

  using value_type = T;
  static constexpr int arity = std::tuple_size<T>::value;
  template<size_t N>
  static
  bool index_cmp(const value_type& l, const value_type& r)
  {
    return
      (std::get<N>(l) < std::get<N>(r)) ||
      (!(std::get<N>(r) < std::get<N>(l)) && l < r);
  }

  typedef flat::set<value_type, bool (*)(const value_type& l, const value_type& r)> index_t;
  template<size_t... Is>
  static constexpr std::array<index_t, arity> make_indices(std::index_sequence<Is...>)
  {
    return { index_t(&index_cmp<Is>)... };
  }
  std::array<index_t, arity> indices = make_indices(std::make_index_sequence<arity>());

  void print(std::ostream& os) const override final
  {
    print_(os, this->all);
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
      os << "\n  " << this->name << "(" << detail::print_tuple<value_type>(row) << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    T it(std::forward<Args>(args)...);
    this->all.insert(it);
    for (int i=0; i<arity; i++)
      indices[i].insert(it);
  }

  template<typename... Selector>
  struct Match {
    using query_type = std::tuple<Selector...>;
    static constexpr int arity = std::tuple_size<query_type>::value;
    static_assert(std::tuple_size<value_type>::value == arity, "Inconsistent lengths");
    static_assert(detail::check_query_t<query_type, value_type, 0, arity>::value, "Type mismatch");

    Relation<value_type>& rel;
    query_type selector;

    void print_common(std::ostream& os) const
    {
      os << rel.name << "(" << detail::print_tuple<query_type>(selector) << ")";
    }

    Match(Relation<value_type>& rel, Selector&&... sels)
      : rel(rel)
      , selector(std::forward<Selector>(sels)...)
    {
    }
  };

  template<typename... Selector>
  struct Match_select  : public Rule::susp_Head, public Rule::susp_Body {
    using Match_base = Match<Selector...>;
    Match_base m;
    Match_select(Relation<value_type>& rel, Selector&&... sels)
      : m(rel, std::move(sels)...)
    {}
    struct Head : public Match_base, Rule::Head {
      static void eval(Rule::Elem& self_, Rule&, size_t)
      {
        Head& self = static_cast<Head&>(self_);
        auto res = transform_each(self.selector, detail::get_value{});
        self.rel.next_delta.insert(std::move(res));
      }
      Head(Match_base&& m): Match_base(std::move(m)), Rule::Head(eval) {}
      void print(std::ostream& os) const override final { this->print_common(os); }
    };

    struct Body : public Match_base, public Rule::Body, private detail::undo_helper {
      void add_undo(Var_* v) override final { this->add_undo_(v); }

      static void eval(Rule::Elem& self_, Rule& r, size_t idx)
      {
        Body& self = static_cast<Body&>(self_);
        for (auto const& row : idx == r.seminaive_current ? self.rel.delta : self.rel.all) {
          if (for_each_in_tuple(detail::unify1(), self.selector, row))
            self.next(r, idx);
          self.undo();
        }
      }
      Body(Match_base&& m): Match_base(std::move(m)), Rule::Body(eval) {}
      void print(std::ostream& os) const override final { this->print_common(os); }
    };

    Rule::vars_t get_vars() const
    {
      return detail::mark_vars(m.selector);
    }

    std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
    {
      Rule::elem_meta meta = { Rule::with_vars(get_vars(), nullptr), &m.rel };
      auto p = flat::allocate<Body>(std::move(m));
      return std::make_pair(meta, p);
    }

    std::pair<Rule::elem_meta, Rule::uhead> apply_Head() override final
    {
      Rule::elem_meta meta = { Rule::with_vars(nullptr, get_vars()), &m.rel };
      auto p = flat::allocate<Head>(std::move(m));
      return std::make_pair(meta, p);
    }
  };

  template<typename... SelectArgs>
  Match_select<typename flat::remove_cvref<SelectArgs>::type...>
  operator()(SelectArgs&&... args) {
    return Match_select<typename flat::remove_cvref<SelectArgs>::type...>(*this, std::forward<typename flat::remove_cvref<SelectArgs>::type>(args)...);
  }
};

template<typename T>
struct Objects : Typed_collection<flat::pool_ptr<T>> {
  friend class DB;
  using Typed_collection<flat::pool_ptr<T>>::Typed_collection;

  using value_type = T;

  void print(std::ostream& os) const override final
  {
    os << "{";
    for (auto const& row : this->all)
      os << "\n  " << this->name << "(" << *row << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    this->all.insert(this->db.pool.template allocate<value_type>(std::forward<Args>(args)...));
  }

  void insert(flat::pool_ptr<T> p) {
    this->all.insert(p);
  }

  struct Match_base {
    Objects<value_type>& rel;
    Var<value_type> that;

    Match_base(Objects<value_type>& rel, Var<value_type>& that)
      : rel(rel)
      , that(std::move(that))
    {}

    void print_common(std::ostream& os) const
    {
      os << "<";
      Var<value_type>::do_print(os, that);
      os << ">";
    }
  };

  struct Match_select : public Rule::susp_Body { // TODO: also Rule::Head
    Match_base m;
    Match_select(Objects<value_type>& rel, Var<value_type>& that)
      : m(rel, that)
    {}

    Rule::elem_meta meta() const noexcept {
      Rule::vars_t vars;
      vars.set(m.that.get_id());
      return { Rule::with_vars(vars, nullptr), &m.rel };
    }
    struct Body : public Match_base, Rule::Body {
      bool bound = false;
      Body(Match_base&& m): Match_base(std::move(m)), Rule::Body(eval_body) {}
      void add_undo(Var_* v) override final
      {
        assert(!v || v->get_id() == Body::that.get_id());
        bound = v;
      }
      void print(std::ostream& os) const override final { this->print_common(os); }
      static void eval_body(Rule::Elem& self_, Rule& r, size_t idx)
      {
        // FIXME: if `bound` is set, just check `rel.contains(*that)`
        Body& self = static_cast<Body&>(self_);
        for (auto const& urow : self.rel.all) {
          if (self.that.unify(*urow)) {
            self.next(r, idx);
          }
          self.that.zap();
        }
      }
    };

    std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
    {
      return {meta(), flat::allocate<Body>(std::move(m))};
    }
  };

  Match_select
  operator()(Var<value_type>& that) {
    return Match_select(*this, that);
  }
};
