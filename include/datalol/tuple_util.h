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

  template <class F, typename Tuple, size_t... Is>
  auto transform_each_impl(const Tuple& t, F&& f, std::index_sequence<Is...>)
  {
    return std::tie(f(tuple_lift<Tuple>::template get<Is>(t) )...);
  }

  template <size_t Is, class F, typename T0, typename... Ts>
  bool for_each_impl(F&& f, const T0& t0, const Ts&... ts)
  {
    return std::forward<F>(f)(Is, tuple_lift<T0>::template get<Is>(t0), tuple_lift<Ts>::template get<Is>(ts)...);
  }

  template <class F, typename T0, typename... Ts, size_t... Is>
  bool for_each_impl(F&& f, std::index_sequence<Is...>, const T0& t0, const Ts&... ts)
  {
    bool res = true;
    using unused = int[];
    (void)unused{0, (res = res && for_each_impl<Is>(std::forward<F>(f), t0, ts...), 0)...};

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
for_each_in_tuple(F&& f, const T0& t0, const Ts&... ts)
{
  static constexpr size_t arity = detail::tuple_lift<T0>::size;
  static_assert(detail::all<(arity == detail::tuple_lift<Ts>::size)...>::value, "All tuples must have the same arity");
  return detail::for_each_impl(std::forward<F>(f), std::make_index_sequence<arity>{}, t0, ts...);
}

} // namespace datalol
