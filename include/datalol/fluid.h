#pragma once

namespace datalol {

template<typename T>
class fluid_var {
  T *p = nullptr;

  struct guard {
    T*& p;
    T* oldval;
    guard(T*& p, T *newval)
      : p(p)
      , oldval(p)
    {
      p = newval;
    }
    ~guard() { p = oldval; }
  };

public:
  fluid_var() = default;
  fluid_var(const fluid_var&) = delete;
  T* get() noexcept { return p; }
  T& operator*() noexcept { return *get(); }
  T* operator->() noexcept { return get(); }

  guard assign(T& t)
  {
    return guard(p, &t);
  }
  guard assign(T&&) = delete;
};

}
