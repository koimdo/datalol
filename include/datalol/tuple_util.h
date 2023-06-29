// -*- C++ -*-
#pragma once

#include <iostream>
#include <memory>
#include <tuple>

// FIXME: remove when we are assured we have C++14 (or higher) also in csp
#ifndef __cpp_lib_make_unique
namespace std {
template<typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) { return unique_ptr<T>(new T(forward<Args>(args)...)); }
}
#endif

namespace detail
{
  template <class F, typename Tuple, size_t... Is>
  auto transform_each_impl(const Tuple& t, F&& f, std::index_sequence<Is...>)
  {
    return std::make_tuple(f(std::get<Is>(t) )...);
  }

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

template <class F, typename... Args>
auto transform_each(const std::tuple<Args...>& t, F&& f)
{
  return detail::transform_each_impl(t, f, std::make_index_sequence<sizeof...(Args)>{});
}

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
