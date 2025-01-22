#pragma once

#include <vector>
#include <algorithm>

namespace datalol {
namespace detail {

template<typename T>
struct span {
  T *beg_, *end_;
  constexpr span(T *beg, T *end): beg_(beg), end_(end) {}
  constexpr span(T *beg, size_t len): span(beg, beg+len) {}
  const T *begin() const { return beg_; }
  const T *end() const { return end_; }
  T *begin() { return beg_; }
  T *end() { return end_; }
  T& operator[](size_t n) { return beg_[n]; }
  const T& operator[](size_t n) const { return beg_[n]; }
  bool empty() const { return beg_ == end_; }
  size_t size() const { return end_ - beg_; }
};

template<typename T, typename Compare>
struct relation {
  using value_type = T;
  using elems_t = std::vector<T>;
  elems_t elements;
  Compare cmp; // FIXME: move `cmp` to the owning table instead of every `relation`?
  relation() = default;
  relation(const Compare& cmp)
    : cmp(cmp)
  {}
  relation(elems_t&& src, const Compare& cmp)
    : elements(std::move(src))
    , cmp(cmp)
  {
    do_dedup(cmp);
  }

  void do_dedup()
  {
    std::sort(elements.begin(), elements.end(), cmp);
    elements.erase(std::unique(elements.begin(), elements.end(), [this](const T& l, const T& r) { return !cmp(l, r) && !cmp(r, l); }), elements.end());    
  }
  void assign(elems_t&& src)
  {
    elements.assign(std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
    do_dedup();
  }

  bool empty() const { return elements.empty(); }
  size_t size() const { return elements.size(); }
  auto begin() const { return elements.begin(); }
  auto end() const { return elements.end(); }

  bool contains(const T& t) const
  {
    auto pos = std::lower_bound(begin(), end(), t, cmp);
    return end() != pos && !cmp(t, *pos);
  }

  static
  void append(elems_t& dst, elems_t& src)
  {
    dst.insert(dst.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
  }

  static
  elems_t merge(elems_t&& l, elems_t&& r, const Compare& cmp)
  {
    if (l.empty()) return std::move(r);
    if (r.empty()) return std::move(l);

    if (cmp(l.back(), r.front())) {
      append(l, r);
      return std::move(l);
    }
    if (cmp(r.back(), l.front())) {
      append(r, l);
      return std::move(r);
    }

    elems_t res;
    res.reserve(l.size() + r.size());
    std::set_union(std::make_move_iterator(l.begin()), std::make_move_iterator(l.end()),
                   std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()),
                   std::back_inserter(res),
                   cmp);
    return res;
  }
  void merge_from(relation&& o)
  {
    elements = merge(std::move(elements), std::move(o.elements), cmp);
  }
};

}
}
