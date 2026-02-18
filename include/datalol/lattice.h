#pragma once

#include <iostream>
#include <limits>
#include "syntax.h"

namespace datalol {

namespace lattice {

struct lattice_tag_base {};
static inline bool operator<(lattice_tag_base, lattice_tag_base) { return false; }
template<typename Value>
struct lattice_tag_t : public lattice_tag_base {
  using lattice_reveal = Value;
  using lattice = lattice_tag_base;
  // Lattice methods, exposition only
  /**
   * Join with un-encapsulated value
   *
   * After completion, the represented value is the LUB of previous value and r, so
   * this->value ← this->value ∨ r
   *
   * @param r Raw underlying value to join with
   */
  void update(const lattice_reveal& r);

  /**
   * Join with encapsulated value
   *
   * After completion, the represented value is the LUB of previous value and r, so
   * this->value ← this->value ∨ r.value
   *
   * @param r Lattice value to join with
   */
  void merge(const lattice& r);

  /**
   * Monus against a value
   *
   * For lattice elements a,b, the monus a∸b is a minimal element c such that a ≤ b∨c.
   *
   * For example, in the (semi-)lattice of sets, the monus is the set difference a\b.
   *
   * this->value ← this->value ∸ r.value
   *
   * A few algebraic properties:
   *
   * b∸a = ⊥ for all b≤a
   * a∸⊥ = a
   *
   * @param r Lattice value to monus @return true if this∸r ≠ ⊥. In case the return value
   * is false, it is permitted to leave this unchanged
   */
  bool monus(const lattice& r);
};

struct bot_t {
  friend std::ostream& operator<<(std::ostream& os, bot_t) { return os << "⊥"; }
};
static constexpr bot_t bot{};

}

template<class L>
class LVar : public detail::Var_ {
  static_assert(std::is_base_of<lattice::lattice_tag_base, L>::value, "Must have lattice type");
  friend class detail::get_value;
  const L *get() const { return &static_cast<const Impl*>(impl)->lattice; }

  template<typename MemPtr, typename Arg>
  bool lattice(MemPtr m, Arg& arg) const
  {
    auto& l = static_cast<const Impl*>(impl)->lattice;
    // After zapping, start with new (i.e. ⟂) lattice
    if (!is_set())
      l = L{};
    (l.*m)(std::forward<Arg>(arg));
    return Var_::set(&l);
  }
public:
  LVar(std::string&& name = {})
    : Var_(make<Impl, L>(std::move(name)))
  {
    static_assert(sizeof(LVar<L>) == sizeof(Var_), "Extra members?");
  }
  LVar(const LVar& v): Var_(v) { register_var(); }
  LVar(LVar&&) = default;

  struct Impl : public Var_::Impl {
    using Var_::Impl::Impl;
    mutable L lattice;

    void print(std::ostream& os) const override
    {
      print_common(os);
      if (p) {
        os << " = " << lattice;
      }
    }
  };

  using value_type = typename L::lattice_reveal;

  bool unify(const value_type& t) const { return lattice(&L::update, t); }
  bool unify(const L& o) const          { return lattice(&L::merge,  o); }

  const value_type& reveal() const noexcept { return get()->reveal(); }
  const value_type& operator*() const noexcept { return reveal(); }
};

namespace detail {

template<typename T>
LVar<T> sel_unwrap(LVar<T>& v) { return std::move(v); }

}

template<typename S, typename T>
detail::Rule::ubody operator==(LVar<S>& v, T&& getter)
{
  return std::forward<T>(getter) == v;
}

namespace lattice {

template<typename T>
class lmin : public lattice_tag_t<T> {
  T t;
public:
  lmin(bot_t = bot): lmin(std::numeric_limits<T>::max()) {}
  lmin(T t)
    : t(t)
  {
  }

  void update(const T& v)
  {
    t = std::min(t, v);
  }

  void merge(const lmin& o)
  {
    t = std::min(t, o.t);
  }

  bool monus(const lmin& o)
  {
    return t < o.t;
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const lmin& m)
  {
    return os << "lmin(" << m.t << ")";
  }

  inline friend bool operator==(const lmin& l, const lmin& r) { return l.t == r.t; }
  const T& reveal() const { return t; }
};

template<typename T>
detail::thunk<lmin<T>> operator+(LVar<lmin<T>>& l, LVar<lmin<T>>& r) { return THUNK(lmin<T>(*l + *r)); }

template<typename T>
detail::thunk<lmin<T>> operator+(LVar<lmin<T>>& l, const T& r) { return THUNK(lmin<T>(*l + r)); }

template<typename T>
detail::thunk<lmin<T>> operator+(const T& l, LVar<lmin<T>>& r) { return THUNK(lmin<T>(l + *r)); }

template<typename T>
class lmax : public lattice_tag_t<T> {
  T t;
public:
  lmax(bot_t = bot): lmax(std::numeric_limits<T>::min()) {}
  lmax(T t)
    : t(t)
  {
  }

  void update(const T& v)
  {
    t = std::max(t, v);
  }

  void merge(const lmax& o)
  {
    t = std::max(t, o.t);
  }

  bool monus(const lmax& o)
  {
    return t > o.t;
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const lmax& m)
  {
    return os << "lmax(" << m.t << ")";
  }

  inline friend bool operator==(const lmax& l, const lmax& r) { return l.t == r.t; }
  const T& reveal() const { return t; }
};

template<typename T>
detail::thunk<lmax<T>> operator+(LVar<lmax<T>>& l, LVar<lmax<T>>& r) { return THUNK(lmax<T>(*l + *r)); }

template<typename T>
detail::thunk<lmax<T>> operator+(LVar<lmax<T>>& l, const T& r) { return THUNK(lmax<T>(*l + r)); }

template<typename T>
detail::thunk<lmax<T>> operator+(const T& l, LVar<lmax<T>>& r) { return THUNK(lmax<T>(l + *r)); }

template<typename T>
class lbitset : public lattice_tag_t<T> {
  T t;
public:
  lbitset(bot_t = bot): lbitset(T{}) {}
  lbitset(T t)
    : t(t)
  {
  }

  void update(const T& v)
  {
    t |= v;
  }

  void merge(const lbitset& o)
  {
    t |= o.t;
  }

  bool monus(const lbitset& o)
  {
    t &= ~o.t;
    return t != 0;
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const lbitset& m)
  {
    return os << "lbitset(" << m.t << ")";
  }

  inline friend bool operator==(const lbitset& l, const lbitset& r) { return l.t == r.t; }
  const T& reveal() const { return t; }
};

template<typename Set>
class lset : public lattice_tag_t<Set> {
  Set set;
public:
  lset(bot_t = bot) {}
  lset(const Set& set): set(set) {}
  lset(typename Set::value_type const& x): set({x}) {}
  void update(const Set& o)
  {
    set = set.set_union(o);
  }
  void merge(const lset& o)
  {
    set = set.set_union(o.set);
  }
  bool monus(const lset& o)
  {
    set = set.diff(o.set);
    return !set.empty();
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const lset& m)
  {
    return os << "lset(" << m.set.size() << ")";
  }

  const Set& reveal() const noexcept { return set; }
};

}
}
