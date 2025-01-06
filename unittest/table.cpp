#include <vector>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <datalol/syntax.h>
#include <memory> // for unique_ptr

using val_t = std::pair<int, std::string>;
std::ostream& operator<<(std::ostream& os, const val_t& v)
{
  return os << "<" << v.first << ", " << v.second << ">";
}

class wrapper {
  const void *p;
#ifndef NDEBUG
  type_id_t type;
#endif
public:
  template<typename T>
  operator T&() { return *get<T>(); }

  template<class T>
  T *get() {
    assert(type_id_t::of<T>() == type);
    return static_cast<T*>(const_cast<void*>(p));
  }

  template<class T>
  wrapper(T *p)
    : p(p)
#ifndef NDEBUG
    , type(type_id_t::of<T>())
#endif
  {}
};

class index_base {
  // Avoid ambiguous overrides in comparators when column type is `int`
  struct nint {
    int i;
    constexpr operator int() const { return i; }
  };
  std::vector<nint> items;

  template<typename Storage, typename Cmp, typename Proj>
  struct cmp {
    const Storage& storage;
    const Cmp& compare;
    const Proj& proj;

    auto get(nint idx) const -> decltype(proj(storage[idx.i])) { return proj(storage[idx.i]); }
    using Key = decltype(std::declval<cmp>().get(nint{0}));
    bool operator()(nint idx, const Key& k) const { return compare(get(idx), k); }
    bool operator()(const Key& k, nint idx) const { return compare(k, get(idx)); }
  };
protected:
  using iterator = std::vector<nint>::const_iterator;

  template<class Storage, class Cmp, typename Proj, class Key>
  std::pair<iterator, bool> insert_impl(const Storage& storage, const Cmp& compare, const Proj& proj,
                                        const Key& key, bool unique) const
  {
    cmp<Storage, Cmp, Proj> cmp{storage, compare, proj};
    // insert at the end of the equal range. For non-unique index, this minimizes the number of moves.
    auto pos = std::upper_bound(items.begin(), items.end(), key, cmp);
    return {pos, (items.begin() == pos || !unique || cmp(*(pos-1), key))};
  }

  template<class Storage, class Cmp, typename Proj>
  std::pair<iterator, iterator> find_impl(const Storage& storage, const Cmp& compare, const Proj& proj,
                                          const decltype(proj(storage[0]))& key) const
  {
    cmp<Storage, Cmp, Proj> cmp{storage, compare, proj};
    auto lo = std::lower_bound(items.begin(), items.end(), key, cmp);
    auto hi = std::upper_bound(lo           , items.end(), key, cmp);
    return { lo, hi };
  }

public:
  virtual ~index_base() {}
  template<typename Storage, typename T>
  bool insert(const Storage& storage, const T&t, bool unique)
  {
    auto itb = insert_(&storage, &t, unique);
    if (itb.second)
      items.insert(itb.first, nint{(int)storage.size()});
    return itb.second;
  }

  template<typename Storage, typename Key, typename F>
  void iterate(const Storage& storage, const Key& key, F&& f) const
  {
    auto lohi = find_(&storage, &key);
    auto lo = lohi.first;
    auto hi = lohi.second;
    std::cerr << "lo=" << lo-items.begin() << ", hi=" << hi-items.begin() << "\n";
    for ( ; lo != hi; ++lo)
      f(storage[*lo]);
  }

  struct identity {
    template<class T>
    T operator()(T t) const { return t; }
  };

private:
  virtual std::pair<iterator, bool> insert_(wrapper storage, wrapper t, bool unique) const = 0;
  virtual std::pair<iterator, iterator> find_(wrapper storage, wrapper t) const = 0;

  friend std::ostream& operator<<(std::ostream& os, const index_base& ind);
};

std::ostream& operator<<(std::ostream& os, const index_base& ind)
{
  for (auto i : ind.items)
    os << " " << i;
  return os;
}

template<typename T, typename Cmp, typename Proj>
class table_index : public index_base {
public:
  using storage_t = std::vector<T>;
  Cmp compare;                  // TODO: EBCO
  Proj proj;

  std::pair<iterator, bool> insert_(wrapper storage, wrapper t, bool unique) const override
  {
    return index_base::insert_impl<storage_t, Cmp, Proj>(storage, compare, proj, proj((const T&)t), unique);
  }

  std::pair<iterator, iterator> find_(wrapper storage, wrapper key) const override
  {
    return index_base::find_impl<storage_t, Cmp, Proj>(storage, compare, proj, key);
  }

  table_index(const Cmp& compare = {}, const Proj& proj = {})
    : index_base()
    , compare(compare)
    , proj(proj)
  {}
};


template<typename T>
class table_base {
  template<size_t idx,
           bool it = (idx < std::tuple_size<T>::value)>
  struct do_cmp;

  template<size_t idx>
  struct do_cmp<idx, true> {
    using Key = typename std::tuple_element<idx, T>::type;
    using Compare = std::less<Key>; // TODO: user-definable

    struct proj {
      auto operator()(const T& t) const -> decltype(std::get<idx>(t)) { return std::get<idx>(t); }
    };
    static std::unique_ptr<index_base> make()
    {
      return std::make_unique<table_index<T, Compare, proj>>();
    }
  };

  template<size_t idx>
  struct do_cmp<idx, false> {
    // Nothing here. would crash but not actually reachable
    static std::unique_ptr<index_base> make() { return nullptr; }
  };

protected:
  std::vector<T> storage;
  static constexpr size_t arity = std::tuple_size<T>::value;
  std::unique_ptr<index_base> indices[arity];

public:
  void insert(const T& it)
  {
    for (auto& ind : indices)
      ind->insert(storage, it, false);
    storage.push_back(it);
  }

  table_base()
  {
    static_assert(arity <= 8, "Too much tuple");
    switch (arity) {
    case 8: indices[7] = do_cmp<7>::make();
    case 7: indices[6] = do_cmp<6>::make();
    case 6: indices[5] = do_cmp<5>::make();
    case 5: indices[4] = do_cmp<4>::make();
    case 4: indices[3] = do_cmp<3>::make();
    case 3: indices[2] = do_cmp<2>::make();
    case 2: indices[1] = do_cmp<1>::make();
    case 1: indices[0] = do_cmp<0>::make();
    case 0:
      break;
    }
  }

  template<typename F>
  void iterate(F&& f) const
  {
    for (auto const& t : storage)
      f(t);
  }

  void print_index(std::ostream& os, const index_base& ind)
  {
    os << ": " << ind;
    //ind.iterate(storage, [&os](const T& t) { os << "\n" << t; });
    os << "\n";
  }
  void print(std::ostream& os)
  {
    for (size_t i=0; i<table_base<T>::arity; i++) {
      os << "idx" << i;
      print_index(os, *indices[i]);
    }
  }
};

template<typename T, typename Cmp = std::less<T>>
class table : table_base<T> {
  table_index<T, Cmp, index_base::identity> uniq;
  using super_t = table_base<T>;
public:
  using value_type = T;
  void insert(const value_type& it)
  {
    if (uniq.insert(this->storage, it, true))
      table_base<T>::insert(it);
  }

  using super_t::iterate;
  void print(std::ostream& os)
  {
    os << "ALL";
    this->print_index(os, uniq);
    table_base<T>::print(os);
  }

  template<typename Key, typename F>
  void iterate(size_t idx, const Key& key, F&& f) const
  {
    assert(idx < super_t::arity);
    super_t::indices[idx]->iterate(super_t::storage, key, std::forward<F>(f));
  }
};

using namespace std;

int main()
{
  auto print_item = [](const val_t& v) { cout << "\n" << v.first << ", " << v.second; };
  vector<std::pair<int, std::string>> init = {{5, "a"}, {1, "d"}, {1, "d"}, {4, "b"}, {3, "c"}, {2, "d"}, {1, "d"}, {8, "d"}, {42, "c"}};

  cout << "TABLE --------------------------\n";
  table<val_t> mytbl;
  for (auto const& it : init)
    mytbl.insert(it);
  mytbl.iterate(print_item);
  cout << "\n";
  mytbl.print(cout);

  cout << "idx1(d):";
  mytbl.iterate(1, std::string("d"), print_item);
  cout << "\n";
  cout << "idx1(c):";
  mytbl.iterate(1, std::string("c"), print_item);
  cout << "\n";
}
