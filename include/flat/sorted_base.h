// -*- C++ -*-
#pragma once

#include "stamp"

namespace flat {

// A common code base class for sequence-adapter sorted containers
template<typename Key, typename Value, typename Compare>
class sorted_base : public stamp {
  using Sequence = std::vector<Value>;

  template<typename Iter, typename K>
  static std::pair<Iter, bool> find_(Iter&& pos, Iter&& end, const K& p) {
    int n = end-pos;
    if (!n)
      return { end, false };
    while (n > 1) {
      int half = n/2;
      auto mid = pos + half;
      pos = Compare{}(*mid, p) ? mid : pos;
      n -= half;
    }
    pos += Compare{}(*pos, p);
    return { pos, end != pos && !Compare{}(p, *pos) };
  }

protected:
  Sequence storage;
  typedef typename Sequence::const_iterator seq_iter;

  template<typename K>
  std::pair<typename Sequence::const_iterator, bool> find_(const K& p) const { return find_(storage.begin(), storage.end(), p); }
  template<typename K>
  std::pair<typename Sequence::iterator,       bool> find_(const K& p)       { return find_(storage.begin(), storage.end(), p); }

  template<class Pred>
  void filter_(sorted_base& res, Pred&& pred) const {
    for (auto const& x : storage)
      if (pred(x))
        res.storage.push_back(x);
  }

public:
  typedef typename Sequence::value_type value_type;
  typedef typename Sequence::reference reference;
  typedef typename Sequence::const_reference const_reference;
  typedef typename Sequence::allocator_type allocator_type;
  typedef typename Sequence::size_type size_type;
  typedef typename Sequence::pointer pointer;
  typedef typename Sequence::const_pointer const_pointer;
  typedef stamp::iterator<typename Sequence::const_iterator> const_iterator;
  typedef stamp::iterator<typename Sequence::iterator> iterator;
  typedef stamp::iterator<typename Sequence::const_reverse_iterator> const_reverse_iterator;
  typedef const_reverse_iterator reverse_iterator;

  sorted_base() = default;

  sorted_base(const sorted_base& o): stamp(), storage(o.storage) {}

  sorted_base(std::initializer_list<value_type> init): sorted_base(init.begin(), init.end()) {}

  template<class It>
  sorted_base(It beg, It end)
  {
    storage.reserve(end-beg);
    insert(beg, end);
  }

  template<class It>
  void insert(It beg, It end)
  {
    for (; beg != end; ++beg)
      insert(*beg);
  }

  std::pair<iterator, bool> insert(value_type&& p) {
    auto itb = find_(p);
    if (!itb.second) {
      itb.first = storage.insert(itb.first, std::forward<value_type>(p));
      invalidate();
    }

    return { mkiter(std::move(itb.first)), !itb.second };
  }

  std::pair<iterator, bool> insert(const value_type& p) {
    auto itb = find_(p);
    if (!itb.second) {
      itb.first = storage.insert(itb.first, p);
      invalidate();
    }

    return { mkiter(std::move(itb.first)), !itb.second };
  }

  bool operator==(const sorted_base& o) const { return &o == this || storage == o.storage; }
  bool operator!=(const sorted_base& o) const { return &o != this && storage != o.storage; }

  const_iterator erase(const_iterator pos) {
    invalidate();
    return mkiter(storage.erase(pos.base()));
  }

  bool erase(const Key& p) {
    auto itb = find_(p);
    if (itb.second) {
      invalidate();
      storage.erase(itb.first);
    }
    return itb.second;
  }

  void clear() {
    if (!empty())
      invalidate();
    storage.clear();
  }

  size_type size() const { return storage.size(); }

  bool empty() const { return storage.empty(); }

  const_iterator begin() const { return mkiter(storage.begin()); }
  const_iterator cbegin() const { return mkiter(storage.begin()); }
  const_iterator end() const { return mkiter(storage.end()); }
  const_iterator cend() const { return mkiter(storage.end()); }

  const_reverse_iterator rbegin() const { return mkiter(storage.rbegin()); }
  const_reverse_iterator rend() const { return mkiter(storage.rend()); }

  const_iterator find(const Key& p) const {
    auto itb = find_(p);
    return mkiter(itb.second ? itb.first : storage.end());
  }
};

}
