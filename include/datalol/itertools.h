#pragma once

#include "type_traits.h"
#include "span.h"

namespace datalol {

struct buf_t {
  const void *beg, *end, *pos;
  template<typename T>
  buf_t(const T *beg, const T *pos, const T *end) : beg(beg), end(end), pos(pos) {}
  template<typename T>
  buf_t(const T *beg, const T *end): buf_t(beg, beg, end) {}
  template<typename T>
  buf_t(const T *beg, size_t sz): buf_t(beg, beg+sz) {}
  buf_t(): beg(nullptr), end(nullptr), pos(nullptr) {}
};

template<typename T>
class stream {
  std::function<buf_t()> more_;
public:
  template<typename Fun>
  explicit stream(Fun&& fun)
    : more_(std::forward<Fun>(fun))
    , buf(more_())
  {
  }

  template<typename Fun>
  explicit stream(const buf_t& buf, Fun&& fun)
    : more_(std::forward<Fun>(fun))
    , buf(buf)
  {
  }
  buf_t buf;

  const buf_t& more()
  {
    return buf = more_();
  }
  void assign(const buf_t& b) noexcept
  {
    buf = b;
  }

  const T *get() const noexcept { return buf.pos; }
  explicit operator bool() const noexcept { return buf.pos != buf.end; }
  const T *operator->() const noexcept { return get; }
  const T& operator*() noexcept { return *static_cast<const T*>(buf.pos); }
  stream& operator++()
  {
    buf.pos = (unsigned char *)buf.pos + sizeof(T);
    if (buf.pos == buf.end)
      more();
    return *this;
  }

};

template<typename Coll>
stream<typename Coll::value_type> generic_iterator(const Coll& coll)
{
  auto pos_ = std::begin(coll);
  auto end_ = std::end(coll);
  return stream<typename Coll::value_type>([=]() mutable -> buf_t {
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
  return stream<T>([]() -> buf_t { return {}; },
                   {beg, size});
}

}
