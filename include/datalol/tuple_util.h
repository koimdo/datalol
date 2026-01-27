// -*- C++ -*-
#pragma once

#include <iostream>
#include <memory>
#include <tuple>

namespace datalol
{

namespace detail
{

  template<typename T, typename = void>
  struct tuple_size_ {
    static constexpr size_t value = 0;
  };

  template<typename T>
  struct tuple_size_<T, decltype(void(std::tuple_size<T>::value))> {
    static constexpr size_t value = std::tuple_size<T>::value;
  };

  template<typename T>
  using tuple_size = tuple_size_<std::decay_t<T>>;

  template <class F, typename Tuple, size_t... Is>
  auto transform_each_impl(const Tuple& t, F&& f, std::index_sequence<Is...>)
  {
    using std::get;
    return std::make_tuple(f(get<Is>(t) )...);
  }

  template <size_t Is, class F, typename T0, typename... Ts>
  bool for_each_impl(F&& f, T0&& t0, Ts&&... ts)
  {
    using std::get;
    return f(Is, get<Is>(std::forward<T0>(t0)), get<Is>(std::forward<Ts>(ts))...);
  }

  template <class F, typename T0, typename... Ts, size_t... Is>
  bool for_each_impl(F&& f, std::index_sequence<Is...>, T0&& t0, Ts&&... ts)
  {
    bool res = true;
    using unused = int[];
    (void)unused{0, (res = res && for_each_impl<Is>(std::forward<F>(f), std::forward<T0>(t0), std::forward<Ts>(ts)...), 0)...};

    return res;
  }

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
for_each_in_tuple(F&& f, T0&& t0, Ts&&... ts)
{
  static constexpr size_t arity = detail::tuple_size<T0>::value;
  static_assert(detail::all<(arity == detail::tuple_size<Ts>::value)...>::value, "All tuples must have the same arity");
  return detail::for_each_impl(std::forward<F>(f), std::make_index_sequence<arity>{}, std::forward<T0>(t0), std::forward<Ts>(ts)...);
}

} // namespace datalol
