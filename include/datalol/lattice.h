#pragma once

#include <vector>
#include <algorithm>

namespace datalol {

template<class T>
struct min_lattice {
  T t = std::numeric_limits<T>::max();
  min_lattice() = default;
  min_lattice(T t): t(t) {}
  bool operator==(min_lattice o) const { return t == o.t; }
  bool operator!=(min_lattice o) const { return t == o.t; }
  min_lattice& operator|=(min_lattice o)
  {
    t = std::min(t, o.t);
    return *this;
  }
  const T& get() const { return t; }
};

}
