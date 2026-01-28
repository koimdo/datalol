#pragma once

#include "syntax.h"
#include "tuple_util.h"

namespace datalol {

struct ignore_t {
  template<typename T>
  bool operator==(T) const { return true; }
  friend std::ostream& operator<<(std::ostream& os, ignore_t) { return os << "ignore"; }
};
static constexpr ignore_t ignore{};

struct with_elements_t {};
static constexpr with_elements_t with_elements{};

namespace detail {

struct unify_ {
  // Elementwise cases
  template<typename R> constexpr bool operator()(size_t, const R& s, const R& r) const { return s == r; }

  template<typename Lattice>
  constexpr bool operator()(size_t, const Var<typename Lattice::lattice_reveal>& s, const Lattice& r) const { return s.unify(r.reveal()); }

  template<typename R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const { return s.unify(r); }
  template<typename R> constexpr bool operator()(size_t, const Var<R>& s, R&& r) const { return s.unify(std::move(r)); }
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
    res.produce += v;
    return true;
  }
};

struct get_value {
  template<typename T>
  auto operator()(const Var<T>& v) const { return std::cref(v.value()); }
  template<typename T>
  auto operator()(const T& t) const { return std::cref(t); }

  struct panic {
    template<typename T>
    [[noreturn]] operator T() const { std::abort(); /* never reached */ }
  };
  panic operator()(ignore_t) const { return panic{}; }
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
  bool operator () (size_t, std::reference_wrapper<T> v)
  {
    const void *addr = std::addressof(v.get());
    os << prefix() << "<" << ident::make<T>().type_name() << " @ " << addr << ">";
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

template<typename T, typename All, typename... Sel>
struct Selector {
  using value_type = T;

  static constexpr bool has_full = std::is_same<All, Var<T>>::value;
  static_assert(sizeof...(Sel) == detail::tuple_size<T>::value || (has_full && sizeof...(Sel) == 0), "Inconsistent lengths");
  static constexpr bool can_construct = std::is_constructible<T, decltype(transform_each(std::declval<std::tuple<Sel...>>(), std::declval<get_value>()))>::value;
  static constexpr bool has_value = has_full || (!any<std::is_same<Sel, ignore_t>::value...>::value && can_construct);

  All all;
  std::tuple<Sel...> sel;
  template<typename... Args>
  Selector(All&& all, Args&&... sel)
    : all(std::move(all))
    , sel(std::forward<Sel>(sel)...)
  {}

  void mark_vars(Rule::elem_meta& meta) const
  {
    mark_vars_ mv{meta};
    for_each_in_tuple(mv, sel);
    mv(0, all);
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const Selector& s)
  {
    if (has_full)
      os << s.all;
    return os << print_tuple<decltype(s.sel)>(s.sel);
  }
};

template<typename T, typename... Sel, typename std::enable_if<Selector<T, ignore_t, Sel...>::has_value>::type* = nullptr>
auto get_selector_value(const Selector<T, ignore_t, Sel...>& s)
{
  return transform_each(s.sel, get_value{});
}

template<typename T, typename... Sel, typename std::enable_if<!Selector<T, ignore_t, Sel...>::has_value>::type* = nullptr>
auto get_selector_value(const Selector<T, ignore_t, Sel...>&)
{
  return get_value::panic{};
}

template<typename T, typename... Sel>
const T& get_selector_value(const Selector<T, Var<T>, Sel...>& s)
{
  return *s.all.get();
}

template<typename T, typename All>
bool unify(const Selector<T, All>& s, const T& row)
{
  unify_ u;
  return u(0, s.all, row);
}

template<typename T, typename All, typename S0, typename... Sel>
bool unify(const Selector<T, All, S0, Sel...>& s, const T& row)
{
  unify_ u;
  return u(0, s.all, row) && for_each_in_tuple(u, s.sel, row);
}

template<typename T>
auto sel_unwrap(T&& s) { return std::forward<T>(s); }
template<typename T>
Var<T> sel_unwrap(Var<T>& v) { return std::move(v); }

template<typename T, typename... Sel>
auto
build_selector(Sel&&... sel)
{
  // Unlike std::make_tuple, we want to preserve std::reference_wrapper as-is
  return Selector<T, ignore_t, typename std::decay<Sel>::type...>(ignore_t{}, sel_unwrap(sel)...);
}

template<typename T, typename... Sel>
auto
build_selector(Var<T>& all, with_elements_t, Sel&&... sel)
{
  // Unlike std::make_tuple, we want to preserve std::reference_wrapper as-is
  return Selector<T, Var<T>, typename std::decay<Sel>::type...>(std::move(all), sel_unwrap(sel)...);
}

template<typename T>
auto
build_selector(Var<T>& all)
{
  return Selector<T, Var<T>>(std::move(all));
}

}

}
