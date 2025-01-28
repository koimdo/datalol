#pragma once

#include "syntax.h"
#include "relation.h"
#include "debug.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <type_traits>

namespace datalol {

struct ignore_t {};
static constexpr ignore_t ignore{};

namespace detail {
  struct unify_ {
    // Elementwise cases
    template<typename R> constexpr bool operator()(size_t, const R& s, const R& r) const { return s == r; }
    template<typename R> constexpr bool operator()(size_t, ignore_t, const R&) const { return true; }
    template<typename R> constexpr bool operator()(size_t, const Var<R>& s, reference<R> r) const { return s.unify(r.get()); }
    template<typename R> constexpr bool operator()(size_t, const Var<R>& s, reference<const R> r) const { return s.unify(r.get()); }
    template<typename R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const { return s.unify(r); }
    template<typename S, typename R>
    constexpr bool operator()(size_t, const S& s, const R& r) const
    {
      static_assert(!std::is_base_of<Var_, S>::value, "Var type mismatch");
      return s == r;
    }
  };

  struct mark_vars_ {
    Rule::elem_meta& res;
    template<typename T> bool operator()(size_t, const T&) { return true; }
    template<typename T> bool operator()(size_t, const Var<T>& v)
    {
      res.produce.set(v.get_id());
      return true;
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
    size_t i = 0;
    const char *prefix() { return i++ ? ", " : ""; }
    template<typename T>
    bool operator () (size_t, T const &v)
    {
      os << prefix() << v;
      return true;
    }
    template<typename T>
    bool operator () (size_t, reference<T> v)
    {
      os << prefix() << "<" << ident::make<T>().type_name() << " @ " << static_cast<const void*>(&v.get()) << ">";
      return true;
    }
    bool operator () (size_t, ignore_t)
    {
      os << prefix() << "ignore";
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

// Backported from C++20
template<typename T>
struct remove_cvref {
  using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<typename Sel, bool has_value = true>
struct eval_helper {
  decltype(std::declval<Sel>().get_value()) value;
  eval_helper(const Sel& sel)
    : value(sel.get_value())
  {}

  template<typename T>
  static
  const T& get_elem(const T& t) { return t; }
  template<typename T>
  static
  const T& get_elem(const std::tuple<const T&>& t) { return std::get<0>(t); }

  // TODO: SFINAE-generalize it according to support of coll.count(value)
  template<typename T>
  bool find_in(const std::vector<T>& vec) const
  {
    return std::find(vec.begin(), vec.end(), get_elem(value)) != vec.end();
  }
  template<typename Coll>
  bool find_in(const Coll& coll) const
  {
    return coll.contains(get_elem(value));
  }
};

template<typename Sel>
struct eval_helper<Sel, false> {
  const Sel& sel;
  eval_helper(const Sel& sel)
    : sel(sel)
  {}
  template<typename Coll>
  bool find_in(const Coll& coll) const
  {
    for (auto const& row : coll) {
      if (sel.unify(row))
        return true;
    }
    return false;
  }
};

template<typename Derived, typename Sel, typename Origin>
struct Matcher_base : public Rule::Body {
  Sel selector;
  Origin& origin;

  using value_type = typename Origin::value_type;

  Matcher_base(Sel&& sel, Origin& origin)
    : Rule::Body(&origin)
    , selector(std::forward<Sel>(sel))
    , origin(origin)
  {
    sel.mark_vars(meta);
  }

  void print_impl(std::ostream& os) const
  {
    os << (is_negative() ? "!" : "") << origin.get_name() << "(" << selector << ")";
  }

  enum query_type {
    FULL,
    POINT,
    NEGATIVE,
  } config = FULL;

  void set_negative()
  {
    if (is_negative())
      return;
    config = NEGATIVE;
    meta.negate_vars();
    meta.negative = true;
  }

  bool is_negative() const { return NEGATIVE == config; }
  void config_impl()
  {
    if (is_negative())
      return;
    if (!undo.count) {
      config = POINT;
    } else {
      config = FULL;
    }
  }

  using helper_t = eval_helper<Sel, Sel::has_value>;
  void eval_neg()
  {
    helper_t aux{selector};
    auto& self = static_cast<Derived&>(*this);
    for (auto& coll : self.get_coll()) {
      if (aux.find_in(coll)) {
        return;
      }
    }
    next(true);
  }

  void eval_point()
  {
    helper_t aux{selector};
    auto& self = static_cast<Derived&>(*this);
    for (auto& coll : self.get_coll()) {
      next(aux.find_in(coll));
    }
  }

  void eval_full()
  {
    auto& self = static_cast<Derived&>(*this);
    for (auto& coll : self.get_coll()) {
      for (auto const& row : coll)
        next(selector.unify(row));
    }
  }

  void eval_impl()
  {
    switch (config) {
    case POINT: return eval_point();
    case FULL: return eval_full();
    case NEGATIVE: return eval_neg();
    }
  }
};

template<typename T, typename... Sel>
struct Selector {
  using value_type = T;

  static_assert(sizeof...(Sel) == detail::tuple_lift<value_type>::size, "Inconsistent lengths");
  static constexpr bool has_value = !any<std::is_same<Sel, ignore_t>::value...>::value;

  std::tuple<Sel...> sel;
  Selector(Sel&&... sel)
    : sel(std::forward<Sel>(sel)...)
  {}

  void mark_vars(Rule::elem_meta& meta) const
  {
    mark_vars_ mv{meta};
    for_each_in_tuple(mv, sel);
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const Selector& s)
  {
    return os << print_tuple<decltype(s.sel)>(s.sel);
  }

  bool unify(const value_type& row) const
  {
    unify_ u;
    return for_each_in_tuple(u, sel, row);
  }

  // TODO: return tuple of const references, suitable for comparison or construction
  auto get_value() const
  {
    return transform_each(sel, detail::get_value{});
  }
};

template<typename T>
auto sel_unwrap(T&& s) { return std::forward<T>(s); }
template<typename T>
Var<T> sel_unwrap(Var<T>& v) { return std::move(v); }

template<typename T, typename... Sel>
auto
build_selector(Sel&&... sel)
{
  // Unlike std::make_tuple, we want to preserve std::reference_wrapper as-is
  return Selector<T, typename std::decay<Sel>::type...>(sel_unwrap(sel)...);
}

// FIXME: use the real machinery in json.h once ready
template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  return Json::Value();
}

template<typename Coll>
struct external_ : public Collection {
  Coll coll;

  Json::Value to_json() const override final
  {
    return Json::Value() << id.get_name() << false << id.type_name();
  }

  Json::Value get_contents() const override final { return get_contents_common(coll /* TODO: columns */); }

  void print(std::ostream& os) const override final
  {
    os << id.get_name() << " = external<" << id.type_name() << ">, size=" << coll.size();
  }
  size_t merge(bool) override final { assert(false && "Cannot merge into external relations"); return 0; }

  using value_type = typename remove_cvref<Coll>::type::value_type;

  external_(const Coll& coll_, const ident& id)
    : Collection(id)
    , coll(coll_) {}

  template<typename Sel>
  struct susp {
    external_& rel;
    Sel selector;

    struct Body : Matcher_base<Body, Sel, external_> {
      using Matcher_base<Body, Sel, external_>::Matcher_base;

      span<typename std::remove_reference<Coll>::type>
      get_coll() const noexcept { return {&this->origin.coll, 1}; }

      void configure() override final { this->config_impl(); }
      void eval() override final { this->eval_impl(); }
      void print(std::ostream& os) const override final { this->print_impl(os); }
    };

    operator Rule::ubody()
    {
      return Query::allocate<Body>(std::move(selector), rel);
    }

    Rule::ubody operator!() {
      auto b = Query::allocate<Body>(std::move(selector), rel);
      b->set_negative();
      return b;
    }
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return susp<decltype(detail::build_selector<value_type>(args...))>{*this, detail::build_selector<value_type>(std::forward<SelectArgs>(args)...)};
  }
};

template<typename T, typename Compare = std::less<void>>
struct table : public Collection {
  static_assert(!std::is_base_of<Var_, T>::value, "Cannot have var type!");
  using value_type = T;

  table(const char *name, const Compare& cmp = Compare{})
    : Collection(ident::make<table>(name))
    , stable(cmp)
    , recent(cmp)
  {}

  relation<T, Compare> stable;
  relation<T, Compare> recent;
  std::vector<T> to_add;

  size_t merge(bool recursive) override final
  {
    if (!recursive) {
      assert(stable.empty() && recent.empty());
      stable.assign(std::move(to_add));
      return 0;
    }
    // TODO: hold multiple `stable` relations, defer merges?
    // FIXME: indices
    stable.merge_from(std::move(recent));
    stable.erase_from(to_add);
    recent.assign(std::move(to_add));
    to_add.clear();
    return !recent.empty();
  }

  void print(std::ostream& os) const override final
  {
    os << this->id.get_name();
    if (recent.empty() && to_add.empty()) {
      print_(os, this->stable, "");
    } else {
      print_(os, this->stable, "stable: ");
      print_(os, this->recent, "recent: ");
      print_(os, this->to_add, "to_add: ");
    }
    // TODO: indices?
  }

  Json::Value to_json() const override final
  {
    return Json::Value() << id.get_name() << true << id.type_name();
  }

  Json::Value get_contents() const override final { return detail::get_contents_common(this->stable /* TODO: columns */); }

  template<typename S>
  void print_(std::ostream& os, const S& s, const char *title) const
  {
    auto name = this->id.get_name();
    os << title << " {";
    for (auto const& row : s)
      os << " " << name << "(" << detail::print_tuple<value_type>(row) << ")";
    os <<" }\n";
  }

  // This class is required, since when we create T(v1, v2, ...), we don't know wheather
  // its in head or body position
  template<typename Sel>
  struct susp {
    table& rel;
    Sel selector;

    struct Head : Rule::Head {
      Sel selector;
      table& rel;
      Head(Sel&& selector, table& rel)
        : Rule::Head(&rel)
        , selector(std::move(selector))
        , rel(rel)
      {
        selector.mark_vars(meta);
        meta.negate_vars();
      }
      void eval() override final
      {
        rel.to_add.push_back(value_type(selector.get_value()));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << selector << ")";
      }
    };

    struct Body : detail::Matcher_base<Body, Sel, table> {
      using detail::Matcher_base<Body, Sel, table>::Matcher_base;
      span<relation<T, Compare>> get_coll() noexcept
      {
        if (this->is_negative())
          return {&this->origin.stable, 1};
        switch (this->use_delta()) {
        case Rule::Body::RECENT: return {&this->origin.recent, 1};
        case Rule::Body::STABLE: return {&this->origin.stable, 1};
        case Rule::Body::BOTH:
          // contiguous members with same access specifier are contiguous in memory.
          // This returns both `stable` and `recent`.
          return {&this->origin.stable, 2};
        }
        return {nullptr, nullptr};
      }

      void configure() override final { this->config_impl(); }
      void eval() override final { this->eval_impl(); }
      void print(std::ostream& os) const override final
      {
        if (!this->is_negative()) {
          switch (this->use_delta()) {
          case Rule::Body::RECENT: os << "δ"; break;
          case Rule::Body::STABLE: break;
          case Rule::Body::BOTH:   os << "δ⁺"; break;
          }
        }
        this->print_impl(os);
      }
    };

    operator Rule::ubody()
    {
      return Query::allocate<Body>(std::move(selector), rel);
    }


    Rule::ubody operator!() {
      auto b = Query::allocate<Body>(std::move(selector), rel);
      b->set_negative();
      return b;
    }

    operator Rule::uhead()
    {
      return Query::allocate<Head>(std::move(selector), rel);
    }
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return susp<decltype(detail::build_selector<value_type>(args...))>{*this, detail::build_selector<value_type>(std::forward<SelectArgs>(args)...)};
  }
};

template<typename Res, typename V>
struct binder_base : public Rule::Body {
  using bound_t = Var<typename std::decay<V>::type>;
  using thunk_t = thunk<Res>;
  thunk_t fun;
  bound_t var;
  binder_base(thunk_t&& fun, bound_t& var)
    : Rule::Body({{}, fun.captured_vars(), nullptr})
    , fun(std::move(fun))
    , var(std::move(var))
  {
    meta.produce.set(var.get_id());
    meta.produce &= ~meta.negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
  }
};

template<typename Res>
struct binder : public binder_base<Res, Res> {
  using binder_base<Res, Res>::binder_base;
  void eval() override final
  {
    auto&& res = this->fun.apply(); // `res` is now alive for the rest of the call chain
    Rule::Body::next(this->var.unify(res));
  }
  void print(std::ostream& os) const override final
  {
    os << this->var << " == " << this->fun;
  }
};

template<typename Res>
struct iterate_ {
  using element_t = decltype(*std::begin(std::declval<Res>()));
  using binder_t = binder_base<Res, element_t>;
  struct body : public binder_t {
    using binder_t::binder_base;
    void eval() override final
    {
      auto&& coll = this->fun.apply(); // `coll` is now alive for the rest of the call chain
      for (auto&& val : coll)
        Rule::Body::next(this->var.set(val));
    }
    void print(std::ostream& os) const override final
    {
      os << this->var << " == iterate(" << this->fun << ")";
    }
  };

  using thunk_t = typename body::thunk_t;
  thunk_t th;
  iterate_(thunk_t&& th)
    : th(std::move(th))
  {}
  Rule::ubody operator==(typename body::bound_t& var)
  {
    return Query::allocate<body>(std::move(th), var);
  }
};

template<typename T, bool = std::is_integral<T>::value>
struct step_iterator;

template<typename T>
struct step_iterator<T, false> {
  T i;
  ptrdiff_t step;
  bool operator!=(const step_iterator& o) { return i != o.i; }
  auto&& operator*() { return *i; }
  step_iterator& operator++() { i += step; return *this; }
};

template<typename T>
struct step_iterator<T, true> {
  T i;
  ptrdiff_t step;
  bool operator!=(step_iterator o) { return i != o.i; }
  auto operator*() { return i; }
  step_iterator& operator++() { i += step; return *this; }
};
static ptrdiff_t sign_of(ptrdiff_t d) { return (d > 0) - (d < 0); }

template<typename T>
struct xrange_ {
  T start, stop;
  ptrdiff_t step;
  using iterator = step_iterator<T>;
  xrange_(T start, T stop, ptrdiff_t step)
    : start(start), stop(stop), step(step)
  {}
  iterator begin() const { return iterator{start, step}; }
  iterator end() const { return iterator{stop, step}; }
};

template<typename Res>
Rule::ubody operator==(thunk<Res>&& t, typename binder<Res>::bound_t& var)
{
  return Query::allocate<binder<Res>>(std::move(t), var);
}

}

template<typename Coll>
class external_ {
  detail::external_<Coll>& impl;
public:
  external_(detail::external_<Coll>& impl)
    : impl(impl)
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
};

template<typename Coll>
external_<Coll> external(Coll&& coll, const char *name)
{
  auto impl = Query::allocate<detail::external_<Coll>>(std::forward<Coll>(coll), ident::make<Coll>(name));
  return external_<Coll>(*impl);
}
template<typename Res>
auto iterate(thunk<Res>&& t)
{
  return detail::iterate_<Res>(std::move(t));
}

template<typename T>
auto xrange(T start, T stop, ptrdiff_t step = 0)
{
  ptrdiff_t len = stop - start;
  if (!step)
    step = detail::sign_of(len);
  auto rem = len % step;
  if (rem)
    stop += (step-rem);
  return detail::xrange_<T>(start, stop, step);
}

template<typename T>
std::enable_if<std::is_integral<T>::value, detail::xrange_<T>>
xrange(T stop)
{
  return xrange(0, stop, 1);
}

template<typename...>
class table;

template<typename T>
class table<T> {
  detail::table<T>& impl;

public:
  table(const char *name)
    : impl(*Query::allocate<detail::table<T>>(name))
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
};

template<typename T0, typename T1, typename... Rest>
struct table<T0, T1, Rest...> : public table<std::tuple<T0, T1, Rest...>> {
  using table<std::tuple<T0, T1, Rest...>>::table;
  static_assert(!std::is_base_of<Var_, T0>::value, "Cannot have var type!");
  static_assert(!std::is_base_of<Var_, T1>::value, "Cannot have var type!");
  static_assert(!detail::any<std::is_base_of<Var_, Rest>::value...>::value, "Cannot have var type");
};

}
