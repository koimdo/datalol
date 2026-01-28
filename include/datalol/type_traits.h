// -*- C++ -*-

#pragma once

namespace datalol {

namespace detail {

#define DETECTOR(detector, expr)                                \
  template<typename T, typename = void>                         \
  struct detector : std::false_type {};                         \
  template<typename T>                                          \
  struct detector<T, decltype(void(expr))> : std::true_type {}

DETECTOR(is_printable, std::declval<std::ostream>() << std::declval<T>());
DETECTOR(is_contextual_bool, std::declval<T>() ? true : false);
DETECTOR(has_arrow, std::declval<T>().operator->());
DETECTOR(has_star, std::declval<T>().operator*());

#undef DETECTOR

template<typename T, bool = has_star<T>::value && has_arrow<T>::value>
struct pointer_helper;

template<typename T>
struct pointer_helper<T, false> {
  const T* arrow(const T* p) const { return p; }
  const T& star(const T* p) const { return *p; }
};

template<typename T>
struct pointer_helper<T, true> {
  decltype(auto) arrow(const T *p) const { return (*p).operator->(); }
  decltype(auto) star(const T *p) const { return (*p).operator*(); }
};

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
