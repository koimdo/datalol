#pragma once

#include <algorithm>
#include <limits>

namespace datalol {
namespace lattice {

template<typename T, typename V>
struct lattice {
  using lift_type = V;
  using reveal_type = T;

  bool operator<(const lattice&) const { return false; } // Lattices are not valid keys. combine instead.
  T& reveal() { return t; }
  const T& reveal() const { return t; }
protected:
  T t;
};

// // A lattice is a type inheriting from lattice_mark, and implements the following 3 methods:
// class Derived : lattice_mark<T, V> {
// public:
//   Derived();                    // initialize to bottom element. is_bot() must return true;
//   bool is_bot() const;          // return true if this is the bottom element
//   void append(V&& vs);              // this = ⋁{lift(vsᵢ) ∣ 0≤i<vs.size()}. vs is not reused by the caller
//   void merge(Derived& o);           // this' = this ∨ o.
//                                     // o = ⋀{c ≤ this∨o ∣ o ≤ this∨c}, e.g. for sets, o = o∖this
//                                     // this = this'
//   const T& reveal() const;          // Reveal internak representation T
// };

// Let's implement a few lattices from the Bloomᴸ paper:
class lbool : public lattice<bool, bool> {
public:

  //bool is_bot() const { return !t; }
  void append(bool b)
  {
    t |= b;
  }
  void merge(lbool& o)
  {
    bool nt = t | o.t;
    o.t = o.t > t;
    t = nt;
  }
};

template<typename T>
class lmax : public lattice<T, T> {
  lmax(T t)
  {
    this->t = t;
  }
public:
  lmax()
    : lmax(std::numeric_limits<T>::min())
  {}
  //bool is_bot() const { return std::numeric_limits<T>::min() == reveal(); }
  void assign(T v)
  {
    this->t = std::max(this->t, v);
  }
  void merge(lmax& o) {
    if (this->t < o.t)
      this->t = o.t;
    else
      o = lmax();
  }
};

template<typename T>
class lmin : public lattice<T, T> {
public:
  lmin(T t = std::numeric_limits<T>::max())
  {
    this->t = t;
  }

  //bool is_bot() const { return std::numeric_limits<T>::max() == t; }
  void assign(T v)
  {
    this->t = std::min(this->t, v);
  }
  void merge(lmin& o) {
    if (this->t > o.t)
      this->t = o.t;
    else
      o = lmin();
  }

  lmin operator+(const lmin& o) const { return lmin(this->t + o.t); }
  lmin operator+(T v) const { return lmin(this->t + v); }
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const lmin<T>& l)
{
  return os << "<lmin " << l.reveal() << ">";
}

}
