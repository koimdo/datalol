#include <vector>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <memory> // for unique_ptr

using val_t = std::pair<int, std::string>;
std::ostream& operator<<(std::ostream& os, const val_t& v)
{
  return os << "<" << v.first << ", " << v.second << ">";
}


class type_id_t {
  typedef type_id_t (*ident_t)();

  ident_t id;
  const char *desc;
  type_id_t(ident_t id, const char *desc): id(id), desc(desc) {}
public:
  bool operator==(type_id_t o) const { return id == o.id; }
  bool operator!=(type_id_t o) const { return id != o.id; }

  template<typename T>
  static
  type_id_t of() { return type_id_t{&of<T>, __PRETTY_FUNCTION__}; }

  void assert_eq(type_id_t o) const
  {
    if (*this != o) {
      std::cerr << "Type mismatch: " << desc << " and " << o.desc << "\n";
      assert(false);
    }
  }
};

class index_base {
protected:
  index_base(type_id_t storage, type_id_t value, type_id_t key)
    : storage_tp(storage)
    , value_tp(value)
    , key_tp(key)
  {}

  // Avoid ambiguous overrides in comparators when column type is `int`
  struct nint {
    int i;
    constexpr operator int() const { return i; }
  };
  std::vector<nint> items;
  using iterator = std::vector<nint>::const_iterator;

public:
  virtual ~index_base() {}
  template<typename Storage, typename T>
  bool insert(const Storage& storage, const T&t, bool unique)
  {
    storage_tp.assert_eq(type_id_t::of<Storage>());
    value_tp.assert_eq(type_id_t::of<T>());
    auto itb = insert_(&storage, &t, unique);
    if (itb.second)
      items.insert(itb.first, nint{(int)storage.size()});
    return itb.second;
  }

  template<typename Storage, typename Key, typename F>
  void iterate(const Storage& storage, const Key& key, F&& f) const
  {
    storage_tp.assert_eq(type_id_t::of<Storage>());
    key_tp.assert_eq(type_id_t::of<Key>());
    auto lohi = find_(&storage, &key);
    iterate(storage, lohi.first, lohi.second, std::forward<F>(f));
  }


  template<typename Storage, typename F>
  void iterate(const Storage& storage, F&& f) const
  {
    storage_tp.assert_eq(type_id_t::of<Storage>());
    iterate(storage, items.begin(), items.end(), std::forward<F>(f));
  }
private:
  virtual std::pair<iterator, bool> insert_(const void* storage, const void* t, bool unique) const = 0;
  virtual std::pair<iterator, iterator> find_(const void* storage, const void* t) const = 0;

  type_id_t storage_tp, value_tp, key_tp; // TODO: debug build only

  friend std::ostream& operator<<(std::ostream& os, const index_base& ind);

  template<typename Storage, typename F>
  void iterate(const Storage& storage, iterator lo, iterator hi, F&& f) const
  {
    std::cerr << "lo=" << lo-items.begin() << ", hi=" << hi-items.begin() << "\n";
    for ( ; lo != hi; ++lo)
      f(storage[*lo]);
  }
};

std::ostream& operator<<(std::ostream& os, const index_base& ind)
{
  for (auto i : ind.items)
    os << " " << i;
  return os;
}

template<typename T, class Key, typename Cmp>
class index : public index_base {
public:
  using storage_t = std::vector<T>;
  Cmp compare;                  // TODO: EBCO

  template<typename S>
  struct my_cmp {
    const storage_t& storage;
    const Cmp& compare;
    bool operator()(nint idx, const S& t) { return compare(storage[idx], t); }
    bool operator()(const S& t, nint idx) { return compare(t, storage[idx]); }
  };

  std::pair<iterator, bool> insert_(const void *storage_, const void *t_, bool unique) const override
  {
    const storage_t& storage = *static_cast<const storage_t*>(storage_);
    const T& t = *static_cast<const T*>(t_);
    my_cmp<T> cmp{storage, compare};
    // insert at the end of the equal range. For non-unique index, this minimizes the number of moves.
    auto pos = std::upper_bound(items.begin(), items.end(), t, cmp);
    return {pos, (items.begin() == pos || !unique || cmp(*(pos-1), t))};
  }


  std::pair<iterator, iterator> find_(const void* storage_, const void* key_) const override
  {
    const storage_t& storage = *static_cast<const storage_t*>(storage_);
    const Key& key = *static_cast<const Key*>(key_);
    my_cmp<Key> cmp{storage, compare};
    auto lo = std::lower_bound(items.begin(), items.end(), key, cmp);
    auto hi = std::upper_bound(lo           , items.end(), key, cmp);
    return { lo, hi };
  }

  index(const Cmp& compare = {})
    : index_base(type_id_t::of<storage_t>(), type_id_t::of<T>(), type_id_t::of<Key>())
    , compare(compare) {}
};


template<typename T>
struct table_base {
  template<size_t idx,
           //typename Cmp = std::less<typename std::tuple_element<idx, T>::type>,
           bool it = (idx < std::tuple_size<T>::value)>
  struct do_cmp;

  template<size_t idx>
  struct do_cmp<idx, true> {
    using Key = typename std::tuple_element<idx, T>::type;
    using Compare = std::less<Key>; // TODO: user-definable

    struct cmp {
      static const Key& proj(const T& t) { return std::get<idx>(t); }


      bool operator()(const T& l  , const T& r  ) const { return Compare{}(proj(l), proj(r)); }
      bool operator()(const T& l  , const Key& r) const { return Compare{}(proj(l), r);       }
      bool operator()(const Key& l, const T& r  ) const { return Compare{}(l      , proj(r)); }
    };
    static std::unique_ptr<index_base> make()
    {
      return std::make_unique<index<T, Key, cmp>>();
    }
  };

  template<size_t idx>
  struct do_cmp<idx, false> {
    // Nothing here. would crash but not actually reachable
    static std::unique_ptr<index_base> make() { return nullptr; }
  };

  std::vector<T> storage;
  static constexpr size_t arity = std::tuple_size<T>::value;
  std::unique_ptr<index_base> indices[arity];

  void insert(const T& it)
  {
    for (size_t i=0; i<arity; i++)
      indices[i]->insert(storage, it, false);
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
    ind.iterate(storage, [&os](const T& t) { os << "\n" << t; });
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
  index<T, T, Cmp> uniq;
  using super_t = table_base<T>;
public:
  using value_type = T;
  void insert(const value_type& it)
  {
    if (uniq.insert(this->storage, it, true))
      table_base<T>::insert(it);
  }

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
  mytbl.print(cout);

  cout << "idx1(d):";
  mytbl.iterate(1, std::string("d"), print_item);
  cout << "\n";
  cout << "idx1(c):";
  mytbl.iterate(1, std::string("c"), print_item);
  cout << "\n";
}
