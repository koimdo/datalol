#pragma once

#include <iostream>
#include <limits>
#include "syntax.h"

namespace datalol {

struct lattice_tag_base {};
template<typename Value>
struct lattice_tag_t : public lattice_tag_base { using lattice_reveal = Value; };

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

  void merge(lmin&& o) {
    if (this->t > o.t)
      this->t = o.t;
    else
      o = lmin();
  }

  lmin operator+(const lmin& o) const { return lmin(this->t + o.t); }
  lmin operator+(T v) const { return lmin(this->t + v); }

  inline friend lmin operator+(T l, const lmin& r) { return l+r.t; }
  inline friend lmin operator+(const lmin& l, const lmin& r) { return l.t+r.t; }

  bool operator<(const lmin&) const { return false; } // FIXME: allow operator< for some lattices, override comparison otherwise
  bool operator==(const lmin& o) const
  {
    return reveal() == reveal();// FIXME: this is required for testing only. remove ASAP.
  }

  inline friend
  std::ostream& operator<<(std::ostream& os, const lmin& m)
  {
    return os << "lmin(" << m.t << ")";
  }

  const T& reveal() const { return t; }
};

}
}
