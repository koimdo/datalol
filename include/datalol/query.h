#pragma once

#include "syntax.h"
#include "debug.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <array>

#include <flat/set>
#include <flat/map>
#include <type_traits>

namespace detail {
  template<class S, class R> struct check_arg   : bool_constant<false> {};
  template<class R> struct check_arg<R,      R> : bool_constant<true> {};
  template<class R> struct check_arg<Var<R>, R> : bool_constant<true> {};

  template<typename T, typename = void>
  struct tuple_lift {
    static constexpr size_t size = 1;

    template<size_t>
    using element_type = T;

    template<size_t I>
    static
    const T& get(const T& t)
    {
      static_assert(I==0, "scalars are singleton tuples");
      return t;
    }
  };

  template<typename T>
  struct tuple_lift<T, decltype(void(std::tuple_size<T>::value))> {
    static constexpr size_t size = std::tuple_size<T>::value;

    template<size_t I>
    using element_type = typename std::tuple_element<I, T>::type;

    template<size_t I>
    static
    auto get(const T& t) -> decltype(std::get<I>(t)) { return std::get<I>(t); }
  };

  template<typename Sel, typename Row, size_t i, size_t size>
  struct check_query_t {
    using SElem = typename tuple_lift<Sel>::template element_type<i>;
    using RElem = typename tuple_lift<Row>::template element_type<i>;
    static constexpr bool check1 = check_arg<SElem, RElem>::value;
    static_assert(check1, "Type mismatch");
    static constexpr bool value = check1 && check_query_t<Sel, Row, i+1, size>::value;
  };

  template<typename Sel, typename Row, size_t size>
  struct check_query_t<Sel, Row, size, size> : bool_constant<true> {};

  template<typename Sel, typename Row, size_t i=0, size_t size=tuple_lift<Sel>::size>
  struct unify {
    using TS = tuple_lift<Sel>;
    using TR = tuple_lift<Row>;
    static bool run(const Sel& s, const Row& r)
    {
      unify u{};
      return u(TS::template get<i>(s),
               TR::template get<i>(r)) &&
        unify<Sel, Row, i+1, size>::run(s, r);
    }
    // Elementwise cases
    template<class R> constexpr bool operator()(const R& s, const R& r) const { return s == r; }
    template<class R> constexpr bool operator()(const Var<R>& s, const R& r) { return s.unify(r); }
  };

  template<typename Sel, typename Row, size_t size>
  struct unify<Sel, Row, size, size> : bool_constant<true> {
    static bool run(const Sel&, const Row&) { return true; }
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

#define BASIC_TYPE(typ) \
  static Json::Value json_of(typ value) { return Json::Value(value); }
  BASIC_TYPE(Json::Int)
  BASIC_TYPE(Json::UInt)
  BASIC_TYPE(Json::Int64)
  BASIC_TYPE(Json::UInt64)
  BASIC_TYPE(double)
  BASIC_TYPE(const char *)
  BASIC_TYPE(const Json::String&)
  BASIC_TYPE(bool)
#undef BASIC_TYPE

  struct generic_json {
    Json::Value& vec;
    template<typename T>
    bool operator () (int i, T const &v)
    {
      vec.append(json_of(v));
      return true;
    }
  };

  template<typename... Args>
  Json::Value json_of(const std::tuple<Args...>& t)
  {
    Json::Value res;
    for_each_in_tuple(generic_json{res}, t);
    return res;
  };
}

template<typename Derived, typename Sel, typename Origin>
struct Matcher_base : public Rule::Body, private detail::undo_helper {
  Sel selector;
  Origin& origin;

  Matcher_base(Sel&& sel, Origin& origin)
    : Rule::Body(run_full)
    , selector(std::forward<Sel>(sel))
    , origin(origin)
  {}

  void add_undo(Var_* v) override final { this->add_undo_(v); }

  void print(std::ostream& os) const override final
  {
    os << origin.get_name() << "(" << detail::print_tuple<Sel>(selector) << ")";
  }

  static
  void run_full(Rule::Elem& self_)
  {
    Derived& self = static_cast<Derived&>(self_);
    for (auto const& row : self.get_coll()) {
      if (detail::unify<Sel, typename Origin::value_type>::run(self.selector, row))
        self.next();
      self.undo();
    }
  }
};

template<typename Sel, typename Origin>
struct Matcher_susp_base {
  using value_type = typename Origin::value_type;
  static_assert(std::tuple_size<Sel>::value == detail::tuple_lift<value_type>::size, "Inconsistent lengths");
  static_assert(detail::check_query_t<Sel, value_type, 0, std::tuple_size<Sel>::value>::value, "Type mismatch");

  Sel selector;
  Origin& origin;
  Matcher_susp_base(Origin& origin, Sel&& sel)
    : selector(std::move(sel))
    , origin(origin)
  {}

  Rule::vars_t get_vars() const
  {
    return detail::mark_vars(selector);
  }
};

template<typename... Sel>
std::tuple<typename flat::remove_cvref<Sel>::type...>
build_selector(Sel&&... sel)
{
  return std::tuple<typename flat::remove_cvref<Sel>::type...>
    (std::forward<typename flat::remove_cvref<Sel>::type>(sel)...);
}

template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  Json::Value res;
  Json::Value& values = (res["values"] = Json::arrayValue);
  for (auto const& t : coll)
    values << detail::json_of(t);
  if (columns.size()) {
    Json::Value& jcolumns = (res["columns"] = Json::arrayValue);
    for (auto const& col : columns)
      jcolumns << col;
  }
  return res;
}

template<typename Coll>
class external_impl : public Collection_base {
  const Coll coll;
public:
  template<typename... Args>
  static external_impl& make(Args&&... args) { return Collection_base::make<external_impl>(std::forward<Args>(args)...); }

  using value_type = typename flat::remove_cvref<Coll>::type::value_type;

  Json::Value to_json() const override final
  {
    return Json::Value() << name << false << GetName<Coll>();
  }

  Json::Value get_contents() const override final { return get_contents_common(coll /* TODO: columns */); }

  void print(std::ostream& os) const override final
  {
    os << name << " = external<" << GetName<value_type>() << ">, size=" << coll.size();
  }
  size_t merge() override final { assert(false && "Cannot merge into external relations"); }
  external_impl(const std::string& name, const Coll& coll_)
    : Collection_base(name)
    , coll(coll_) {}

  template<typename Sel>
  struct susp : public Matcher_susp_base<Sel, external_impl>, public Rule::susp_Body {
    using super_t = Matcher_susp_base<Sel, external_impl>;
    using super_t::Matcher_susp_base;
    struct Body : Matcher_base<Body, Sel, external_impl> {
      using Matcher_base<Body, Sel, external_impl>::Matcher_base;
      const Coll& get_coll() const noexcept { return this->origin.coll; }
    };

    std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
    {
      Rule::elem_meta meta = { Rule::with_vars(super_t::get_vars(), nullptr), &this->origin };
      auto p = flat::allocate<Body>(std::move(super_t::selector), super_t::origin);
      return std::make_pair(meta, p);
    }
  };
};

template<typename Coll>
class external_;

template<typename Coll>
external_<const Coll&> external_ref(const char *name, Coll&& coll);

template<typename Coll>
external_<Coll> external_copy(const char *name, Coll&& coll);

template<typename Coll>
class external_ {
  friend external_<const Coll&> external_ref<Coll>(const char *name, Coll&& coll);
  friend external_<Coll> external_copy<Coll>(const char *name, Coll&& coll);

  using Impl = external_impl<Coll>;
  Impl& impl;

  external_(Impl& impl)
    : impl(impl)
  {}

public:
  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) -> typename Impl::susp<decltype(build_selector(std::forward<SelectArgs>(args)...))> {
    return typename Impl::susp<decltype(build_selector(args...))>(impl, build_selector(std::forward<SelectArgs>(args)...));
  }
};

template<typename Coll>
external_<const Coll&> external_ref(const char *name, Coll&& coll)
{
  static_assert(std::is_lvalue_reference<Coll>::value, "Not a reference");
  return external_<const Coll&>::Impl::make(name, coll);
}

template<typename Coll>
external_<Coll> external_copy(const char *name, Coll&& coll)
{
  return external_<Coll>::Impl::make(name, coll);
}

template<typename... Args>
struct table_ : Collection_base {
  using Collection_base::Collection_base;

  template<typename... MArgs>
  static table_& make(MArgs&&... args) { return Collection_base::make<table_>(std::forward<MArgs>(args)...); }

  static_assert(!detail::any<std::is_base_of<Var_, Args>::value...>::value, "Cannot have var type");
  using value_type = std::tuple<Args...>;
  static constexpr int arity = sizeof...(Args);


  flat::set<value_type> all;
  flat::set<value_type> delta, next_delta;

  void append(value_type&& t)
  {
    if (!all.contains(t))
      next_delta.insert(std::move(t));
  }

  size_t merge() override final
  {
    // std::cerr << "Next delta " << name << " : " << next_delta.size() << "\n";
    // std::cerr << "Merging " << name << " delta: ";
    // //print_(std::cerr, delta);
    // std::cerr <<"\n";
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

  void print(std::ostream& os) const override final
  {
    print_(os, this->all);
    // TODO: indices?
  }

  Json::Value to_json() const override final
  {
    return Json::Value() << name << true << GetName<table_>();
  }

  Json::Value get_contents() const override final { return get_contents_common(this->all /* TODO: columns */); }

  template<class S>
  void print_(std::ostream& os, const S& s) const
  {
    os << "{";
    for (auto const& row : s)
      os << "\n  " << this->name << "(" << detail::print_tuple<value_type>(row) << ")";
    os <<"\n}";
  }

  void insert(Args&&... args) {
    value_type it(std::forward<Args>(args)...);
    this->all.insert(it);
  }

  template<typename Sel>
  struct susp : public Matcher_susp_base<Sel, table_>, public Rule::susp_Head, public Rule::susp_Body {
    using super_t = Matcher_susp_base<Sel, table_>;
    using super_t::Matcher_susp_base;

    struct Head : Rule::Head {
      Sel selector;
      table_& rel;
      Head(Sel&& selector, table_& rel)
        : Rule::Head(eval)
        , selector(std::move(selector))
        , rel(rel)
      {}
      static void eval(Rule::Elem& self_)
      {
        Head& self = static_cast<Head&>(self_);
        auto res = transform_each(self.selector, detail::get_value{});
        self.rel.append(std::move(res));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << detail::print_tuple<Sel>(selector) << ")";
      }
    };

    struct Body : Matcher_base<Body, Sel, table_> {
      using Matcher_base<Body, Sel, table_>::Matcher_base;
      const flat::set<value_type>& get_coll() noexcept
      {
        return this->rule().use_delta() ? this->origin.delta : this->origin.all;
      }
    };

    std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
    {
      Rule::elem_meta meta = { Rule::with_vars(super_t::get_vars(), nullptr), &this->origin };
      auto p = flat::allocate<Body>(std::move(super_t::selector), super_t::origin);
      return std::make_pair(meta, p);
    }

    std::pair<Rule::elem_meta, Rule::uhead> apply_Head() override final
    {
      Rule::elem_meta meta = { Rule::with_vars(nullptr, super_t::get_vars()), &this->origin };
      auto p = flat::allocate<Head>(std::move(super_t::selector), super_t::origin);
      return std::make_pair(meta, p);
    }
  };
};

template<typename... Args>
class table {
  using Impl = table_<Args...>;
  Impl& impl;
public:
  table(const char *name)
    : impl(Impl::make(name))
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) -> typename Impl::susp<decltype(build_selector(std::forward<SelectArgs>(args)...))> {
    return typename Impl::susp<decltype(build_selector(args...))>(impl, build_selector(std::forward<SelectArgs>(args)...));
  }
};
