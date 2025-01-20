// -*- C++ -*-
#pragma once

#include <vector>

namespace datalol {
namespace detail {

class pool {
  struct entry {
    void *p;
    typedef void (*destructor_t)(void*);
    destructor_t destroy;
  };
  std::vector<entry> items;

public:
  pool();
  pool(const pool&) = delete;
  pool(pool&&) = delete;
  ~pool();

  template<typename T>
  class wrap {
    T *p;
    friend class pool;
    wrap(T *p): p(p) {}
  public:
    template<typename S>
    operator wrap<S>() { return p; }

    T *get() { return p; }
    T* operator->() { return p; }
    T& operator*() { return *p; }
  };

  template<typename T, typename... Args>
  wrap<T> allocate(Args&&... args) {
    T *t = ::new T(std::forward<Args>(args)...);
    entry::destructor_t destroy;
    if (std::is_trivially_destructible<T>::value)
      destroy = nullptr;
    else
      destroy = [](void *p) { static_cast<T*>(p)->~T(); };
    items.push_back(entry{t, destroy});
    return t;
  }
};

}
}
