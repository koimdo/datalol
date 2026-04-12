#pragma once

#include "type_traits.h"
#include "span.h"
#include "syntax.h"

namespace datalol {

template<typename T>
class stream {
  std::function<detail::span<const T>()> more_;
public:
  template<typename Fun>
  explicit stream(Fun&& fun)
    : more_(std::forward<Fun>(fun))
    , buf(more_())
  {
  }

  template<typename Fun>
  explicit stream(const detail::span<const T>& buf, Fun&& fun)
    : more_(std::forward<Fun>(fun))
    , buf(buf)
  {
  }
  detail::span<const T> buf;

  const detail::span<const T>& more()
  {
    return buf = more_();
  }
  void assign(const detail::span<const T>& b) noexcept
  {
    buf = b;
  }

  const T *get() const noexcept { return buf.data(); }
  explicit operator bool() const noexcept { return !buf.empty(); }
  const T *operator->() const noexcept { return get(); }
  const T& operator*() noexcept { return buf.front(); }
  stream& operator++()
  {
    ++buf;
    if (buf.empty())
      more();
    return *this;
  }

};

template<typename Coll>
stream<typename Coll::value_type> generic_iterator(const Coll& coll)
{
  auto pos_ = begin(coll);
  auto end_ = end(coll);
  using value_type = typename Coll::value_type;
  return stream<value_type>([=]() mutable -> detail::span<const value_type> {
    if (pos_ != end_) {
      auto *t = std::addressof(*pos_);
      ++pos_;
      return {t, t+1};
    } else {
      return {};
    }
  });
}

template<typename T>
struct iterable {
  virtual stream<T> iterator() const = 0;
  virtual bool contains(const T&) const = 0;
};


template<typename T>
stream<T> contiguous_iter(const T *beg, size_t size)
{
  return stream<T>([]() -> detail::span<const T> { return detail::span<const T>(); },
                   {beg, size});
}

}
