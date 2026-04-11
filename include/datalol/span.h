#pragma once

#include <cstddef>
#include <type_traits>
#include <iterator>

namespace datalol {
namespace detail {

template<typename T>
struct span {
  using byte_ptr = typename std::conditional<std::is_const<T>::value, const char*, char*>::type;

  template<typename U>
  struct span_iterator {
    using iterator_category = std::random_access_iterator_tag;
    using value_type = std::remove_const_t<U>;
    using difference_type = std::ptrdiff_t;
    using pointer = U*;
    using reference = U&;

    using byte_ptr = typename std::conditional<std::is_const<U>::value, const char*, char*>::type;

    U* ptr_;
    size_t stride_;

    span_iterator(): ptr_(nullptr), stride_(sizeof(value_type)) {}
    span_iterator(U* p, size_t stride): ptr_(p), stride_(stride) {}

    reference operator*() const { return *ptr_; }
    pointer operator->() const { return ptr_; }

    span_iterator& operator++() {
      ptr_ = reinterpret_cast<U*>(reinterpret_cast<byte_ptr>(ptr_) + stride_);
      return *this;
    }
    span_iterator operator++(int) { span_iterator tmp = *this; ++*this; return tmp; }

    span_iterator& operator--() {
      ptr_ = reinterpret_cast<U*>(reinterpret_cast<byte_ptr>(ptr_) - stride_);
      return *this;
    }
    span_iterator operator--(int) { span_iterator tmp = *this; --*this; return tmp; }

    span_iterator& operator+=(difference_type n) {
      ptr_ = reinterpret_cast<U*>(reinterpret_cast<byte_ptr>(ptr_) + n * stride_);
      return *this;
    }
    span_iterator& operator-=(difference_type n) { return *this += -n; }

    span_iterator operator+(difference_type n) const { span_iterator tmp = *this; return tmp += n; }
    span_iterator operator-(difference_type n) const { span_iterator tmp = *this; return tmp -= n; }
    difference_type operator-(const span_iterator& other) const {
      return static_cast<difference_type>(
        (reinterpret_cast<byte_ptr>(ptr_) - reinterpret_cast<byte_ptr>(other.ptr_)) / static_cast<difference_type>(stride_));
    }

    reference operator[](difference_type n) const { return *(*this + n); }

    bool operator==(const span_iterator& o) const { return ptr_ == o.ptr_; }
    bool operator!=(const span_iterator& o) const { return ptr_ != o.ptr_; }
    bool operator<(const span_iterator& o) const { return ptr_ < o.ptr_; }
    bool operator>(const span_iterator& o) const { return ptr_ > o.ptr_; }
    bool operator<=(const span_iterator& o) const { return ptr_ <= o.ptr_; }
    bool operator>=(const span_iterator& o) const { return ptr_ >= o.ptr_; }
  };

  using iterator = span_iterator<T>;
  using const_iterator = span_iterator<const T>;

  T *beg_;
  size_t size_;
  size_t stride_;  // byte stride between consecutive elements

  constexpr span(): beg_(nullptr), size_(0), stride_(sizeof(T)) {}
  constexpr span(T *beg, size_t size, size_t stride = sizeof(T)): beg_(beg), size_(size), stride_(stride) {}

  // Range-based constructor (assumes contiguous elements)
  constexpr span(T *beg, T *end): beg_(beg), size_(beg <= end ? end - beg : 0), stride_(sizeof(T)) {}

  template< std::size_t N >
  constexpr span( T (&arr)[N] ) noexcept: beg_(arr), size_(N), stride_(sizeof(T)) {}

  T* data() { return beg_; }
  const T* data() const { return beg_; }

  T& front() { return *beg_; }
  const T& front() const { return *beg_; }

  iterator begin() { return iterator(beg_, stride_); }
  iterator end() { return iterator(reinterpret_cast<T*>(reinterpret_cast<byte_ptr>(beg_) + size_ * stride_), stride_); }
  const_iterator begin() const { return const_iterator(beg_, stride_); }
  const_iterator end() const { return const_iterator(reinterpret_cast<const T*>(reinterpret_cast<byte_ptr>(beg_) + size_ * stride_), stride_); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  T& operator[](size_t n) {
    return *reinterpret_cast<T*>(reinterpret_cast<byte_ptr>(beg_) + n * stride_);
  }
  const T& operator[](size_t n) const {
    return *reinterpret_cast<const T*>(reinterpret_cast<byte_ptr>(beg_) + n * stride_);
  }

  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  size_t stride() const { return stride_; }

  span& operator++() {
    assert(size_ > 0);
    beg_ = reinterpret_cast<T*>(reinterpret_cast<byte_ptr>(beg_) + stride_);
    size_--;
    return *this;
  }

  template<typename M, typename U = T>
  span<std::conditional_t<std::is_const<T>::value, const M, M>>
  operator->*(M U::*mp) noexcept {
    return { std::addressof(beg_->*mp), size_, stride_ };
  }
};

template<typename T>
typename span<T>::iterator operator+(typename span<T>::difference_type n, const typename span<T>::iterator& it) {
  return it + n;
}
template<typename T>
typename span<T>::const_iterator operator+(typename span<T>::const_iterator::difference_type n, const typename span<T>::const_iterator& it) {
  return it + n;
}

}
}
