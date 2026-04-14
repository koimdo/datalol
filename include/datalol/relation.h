#pragma once

#include <vector>
#include <algorithm>
#include "span.h"

namespace datalol {
namespace detail {

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
  Combine comb;
  relation(const Compare& cmp = {}, const Combine& comb = {})
    : cmp(cmp)
    , comb(comb)
  {}

  template<typename Iter>
  relation(Iter first, Iter last, const Compare& cmp = {}, const Combine& comb = {})
    : relation(elems_t(first, last), cmp, comb)
  {
  }

  relation(elems_t&& src, const Compare& cmp = {}, const Combine& comb = {})
    : elements(std::move(src))
    , cmp(cmp)
  {
    deduplicate(elements, cmp, comb);
  }

  void assign(elems_t&& src)
  {
    elements = std::move(src);
    deduplicate(elements, cmp, comb);
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

  template<typename K>
  const T *find(const K& t) const
  {
    auto pos = std::lower_bound(begin(), end(), t, cmp);
    return end() != pos && !cmp(t, *pos) ? std::addressof(*pos) : nullptr;
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
        // lᵢ<rⱼ⇒ append lᵢ
        res.push_back(*lbeg);
        ++lbeg;
      } else if (cmp(*rbeg, *lbeg)) {
        // rⱼ<lᵢ⇒ append rᵢ
        res.push_back(*rbeg);
        ++rbeg;
      } else {
        // lᵢ=rⱼ⇒ append combine(lᵢ, rⱼ)
        res.push_back(*lbeg);
        combine(res.back(), *rbeg);
        ++lbeg;
        ++rbeg;
      }
    }
    res.insert(res.end(), rbeg, rend);
    return res;
  }
  void merge_from(relation&& o)
  {
    elements = merge(std::move(elements), std::move(o.elements), cmp, comb);
  }

  void erase_from(elems_t& to_add)
  {
    to_add.erase(std::remove_if(to_add.begin(), to_add.end(),
                                [this](T& x) {
                                  auto o = find(x);
                                  return o && !comb.monus(x, *o);
                                }),
                 to_add.end());
  }

  span<const T> contents() const noexcept { return {elements.data(), elements.size()}; }

  std::pair<int, const T*> find_needle(const void *p) const
  {
    auto* begin = reinterpret_cast<const char*>(elements.data());
    auto* end = begin + elements.size() * sizeof(T);
    auto* ptr = reinterpret_cast<const char*>(p);
    if (ptr >= begin && ptr < end) {
      auto idx = (ptr - begin) / sizeof(T);
      return {idx, std::addressof(elements[idx])};
    }
    return {-1, nullptr};
  }
};

}
}
