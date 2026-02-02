#pragma once

#include <iostream>
#include <limits>
#include "syntax.h"

namespace datalol {

struct lattice_tag_base {};
static inline bool operator<(lattice_tag_base, lattice_tag_base) { return false; }
template<typename Value>
struct lattice_tag_t : public lattice_tag_base { using lattice_reveal = Value; };

template<class L>
class LVar : public detail::Var_ {
  static_assert(std::is_base_of<lattice_tag_base, L>::value, "Must have lattice type");
  friend class detail::get_value;
  const L *get() const { return &static_cast<const Impl*>(impl)->lattice; }
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
      os << " = " << lattice;
    }
  };

  using value_type = typename L::lattice_reveal;

  bool unify(const value_type& t) const
  {
    auto& l = static_cast<const Impl*>(impl)->lattice;
    l.update(t);
    Var_::set(&l);
    return true;
  }

  bool unify(const L& o) const
  {
    auto& l = static_cast<const Impl*>(impl)->lattice;
    l.merge(o);
    Var_::set(&l);
    return true;
  }

  const value_type& reveal() const { return get()->reveal(); }
};

template<typename S, typename T>
detail::Rule::ubody operator==(LVar<S>& v, T&& getter)
{
  return std::forward<T>(getter) == v;
}

namespace lattice {

template<typename T>
class lmin : public datalol::lattice_tag_t<T> {
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

  const T& reveal() const { return t; }
};

template<typename T>
detail::thunk<lmin<T>> operator+(const LVar<lmin<T>>& l, const LVar<lmin<T>>& r) { return THUNK(lmin<T>(l.reveal() + r.reveal())); }

template<typename T>
detail::thunk<lmin<T>> operator+(const LVar<lmin<T>>& l, const T& r) { return THUNK(lmin<T>(l.reveal() + r)); }

template<typename T>
detail::thunk<lmin<T>> operator+(const T& l, const LVar<lmin<T>>& r) { return THUNK(lmin<T>(l + r.reveal())); }

}
}
