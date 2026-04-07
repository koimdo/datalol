#pragma once

namespace datalol {
namespace detail {

template<typename T>
struct span {
  T *beg_, *end_;
  constexpr span(): span(nullptr, nullptr) {}
  constexpr span(T *beg, T *end): beg_(beg), end_(end) {}
  constexpr span(T *beg, size_t len): span(beg, beg+len) {}

  template< std::size_t N >
  constexpr span( T (&arr)[N] ) noexcept: span(arr, arr+N) {}

  const T *begin() const { return beg_; }
  const T *end() const { return end_; }
  T *begin() { return beg_; }
  T *end() { return end_; }
  T& operator[](size_t n) { return beg_[n]; }
  const T& operator[](size_t n) const { return beg_[n]; }
  bool empty() const { return beg_ == end_; }
  size_t size() const { return end_ - beg_; }
  span& operator++() { return ++beg_, *this; }
};

}
}
