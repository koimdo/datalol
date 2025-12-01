// -*- C++ -*-

#pragma once

namespace datalol {

namespace detail {

template<typename T, typename = void>
struct is_printable : std::false_type {};
template<typename T>
struct is_printable<T, decltype(void(std::declval<std::ostream>() << std::declval<T>()))> : std::true_type {};

template<typename T, typename = void>
struct is_contextual_bool : std::false_type {};
template<typename T>
struct is_contextual_bool<T, decltype(void(std::declval<T>() ? true : false))> : std::true_type {};

template<typename T, typename = void>
struct equal_to {
  bool operator()(const T& l, const T& r) const { return !(l < r) && !(r < l); }
};
template<typename T>
struct equal_to<T, decltype(void(std::declval<T>() == std::declval<T>()))> {
  bool operator()(const T& l, const T& r) const { return l == r; }
};


}
}
