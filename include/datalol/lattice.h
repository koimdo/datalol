#pragma once

#include <iostream>
#include <limits>
#include "syntax.h"

namespace datalol {

namespace lattice {

struct lattice_tag_base {};
static inline bool operator<(lattice_tag_base, lattice_tag_base) { return false; }
template<typename Value>
struct lattice_tag_t : public lattice_tag_base { using lattice_reveal = Value; };

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
  lmin(T t = std::numeric_limits<T>::max())
    : t(t)
  {
  }

  void update(const T& v)
  {
    this->t = std::min(this->t, v);
  }

  void merge(const lmin& o) {
    t = std::min(t, o.t);
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

}
}
