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

struct trivial_combine {
  template<typename L, typename R>
  void operator()(L&, R&&) const {}
};

template<typename T, typename Compare, typename Combine = trivial_combine>
struct relation {
  using value_type = T;
  using elems_t = std::vector<T>;
  elems_t elements;
  Compare cmp; // FIXME: move `cmp` to the owning table instead of every `relation`?
  relation(const Compare& cmp = Compare{})
    : cmp(cmp)
  {}

  template<typename Iter>
  relation(Iter first, Iter last, const Compare& cmp = {})
    : relation(elems_t(first, last), cmp)
  {
  }

  relation(elems_t&& src, const Compare& cmp = {})
    : elements(std::move(src))
    , cmp(cmp)
  {
    deduplicate(elements, cmp);
  }

  void assign(elems_t&& src)
  {
    elements = std::move(src);
    deduplicate(elements, cmp);
  }

  bool empty() const { return elements.empty(); }
  size_t size() const { return elements.size(); }
  auto begin() const { return elements.begin(); }
  auto end() const { return elements.end(); }

  template<typename K>
  bool contains(const K& t) const
  {
    auto pos = std::lower_bound(begin(), end(), t, cmp);
    return end() != pos && !cmp(t, *pos);
  }

  static
  void deduplicate(elems_t& elements, const Compare& cmp = {}, const Combine& combine = Combine{})
  {
    if (elements.empty())
      return;

    auto first = elements.begin(), last = elements.end();
    std::sort(first, last, cmp);
    first = std::adjacent_find(first, last, [&cmp](const T& l, const T& r) { return !cmp(l, r); });

    if (first == last)
      return;

    auto out = first;
    ++first;
    while (++first != last)
      if (cmp(*out, *first))
        *++out = std::move(*first);
      else
        // since the range is sorted, l≤r, then if l≮r then l=r. merge equivalents.
        combine(*out, std::move(*first));

    ++out;
    elements.erase(out, last);
  }

  static
  void append(elems_t& dst, elems_t& src)
  {
    dst.insert(dst.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
  }

  static
  elems_t merge(elems_t&& l, elems_t&& r, const Compare& cmp, const Combine& combine = Combine{})
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
    auto lbeg = std::make_move_iterator(l.begin()), lend = std::make_move_iterator(l.end());
    auto rbeg = std::make_move_iterator(r.begin()), rend = std::make_move_iterator(r.end());
    while (lbeg != lend) {
      if (rbeg == rend) {
        res.insert(res.end(), lbeg, lend);
        return res;
      }
      if (cmp(*lbeg, *rbeg)) {
        // l_i < r_j
        res.push_back(*lbeg++);
      } else {
        res.push_back(*rbeg);
        if (!cmp(*rbeg, *lbeg)) {
          // equivalent keys
          combine(res.back(), *lbeg++);
        }
        ++rbeg;
      }
    }
    res.insert(res.end(), rbeg, rend);
    return res;
  }
  void merge_from(relation&& o)
  {
    elements = merge(std::move(elements), std::move(o.elements), cmp);
  }

  void erase_from(elems_t& to_add)
  {
    to_add.erase(std::remove_if(to_add.begin(), to_add.end(),
                                [this](const T& x) {
                                  return contains(x);
                                }),
                 to_add.end());
  }

  const elems_t& data() const noexcept { return elements; }
};

}
}
