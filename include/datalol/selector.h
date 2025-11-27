#pragma once

#include "syntax.h"
#include "tuple_util.h"

namespace datalol {

struct ignore_t {
  template<typename T>
  operator const T&() const { return *static_cast<const T*>(nullptr); }
};
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

}

}
