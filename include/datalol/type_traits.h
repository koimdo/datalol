// -*- C++ -*-

#pragma once

namespace datalol {

template<typename T>
class reference {
  T *p;
public:
  reference(T& t): p(&t) {}
  reference(const reference&) noexcept = default;
  reference& operator=(const reference&) noexcept = default;
  constexpr operator T& () const noexcept { return *p; }
  constexpr T& get() const noexcept { return *p; }
};

template<typename L, typename R>
bool operator==(reference<L> l, R&& r) { return l.get() == r; }
template<typename L, typename R>
bool operator==(L&& l, reference<R> r) { return l == r.get(); }
template<typename L, typename R>
bool operator==(reference<L> l, reference<R> r) { return l.get() == r.get(); }

template<typename L, typename R>
bool operator<(reference<L> l, R&& r) { return l.get() < r; }
template<typename L, typename R>
bool operator<(L&& l, reference<R> r) { return l < r.get(); }
template<typename L, typename R>
bool operator<(reference<L> l, reference<R> r) { return l.get() < r.get(); }

template<typename T>
reference<T> ref(T& t) { return t; }
template<typename T>
reference<T> ref(reference<T> t) { return t; }

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
