// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
#include <bitset>
#include "flat/memory"
#include "flat/span"
#include "flat/set"

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  friend std::ostream& operator<<(std::ostream& os, const IPrint& p) { return p.print(os), os; }
  virtual ~IPrint() {}
};

class Collection_base;
class Var_;
class Rule : public IPrint {
public:
  typedef std::bitset<64> vars_t;
  struct with_vars {
    vars_t positive, negative;
    with_vars();

    template<typename Make>
    static
    auto capture(Make&& make) -> decltype(make())
    {
      Rule::vars_t vars;
      flat::guard vars_guard = capture_helper(&vars);
      return make();
    }
  private:
    static
    flat::guard capture_helper(Rule::vars_t *dst);
  };

  struct Elem : IPrint, public with_vars {
    typedef void (*eval_t)(Elem&, Rule&, size_t);
    eval_t eval_ = nullptr;
    Elem *next = nullptr;
    Elem(eval_t eval_);
    void eval(Rule& r, size_t idx) { (*eval_)(*this, r, idx); }
    virtual Collection_base *collection() const { return nullptr; }
    virtual void add_undo(Var_*) { assert(false && "Must implement add_undo() if it has positive vars"); }
    Elem(const Elem&) = delete;
  };
  struct Head : Elem {
    eval_t eval_head;
    Head(eval_t eval): Elem(nullptr), eval_head(eval) {}
    Head(eval_t head, eval_t body): Elem(body), eval_head(head) {}
  };

  using uhead = flat::pool_ptr<Head>;
  using ubody = flat::pool_ptr<Elem>;

  friend Rule& operator<<(uhead head, ubody b);
  friend Rule& operator& (Rule& rule, Rule::ubody e);
  friend class Query;

  uhead get_head() { return head; }
  flat::span<ubody> get_body() { return body; }
  size_t size() const { return body.size(); }

  size_t seminaive_current;     // FIXME: finer choice of Delta'd relation
  explicit Rule(uhead head);
private:
  void run(size_t current_delta);
  void append(ubody b);
  void print(std::ostream& os) const override;
  uhead head;
  std::vector<ubody> body;
  std::bitset<128> recursive;
};

Rule& operator<<(Rule::uhead head, Rule::ubody e);
Rule& operator& (Rule& rule, Rule::ubody e);

class cow_buf {
  const void *p = nullptr;
  void (*destroy)(const void *) = nullptr; // Not NULL if both owning and non-trivial dtor

public:
  constexpr explicit operator bool() const noexcept { return p; }

  ~cow_buf();
  void clear();

  template<class T>
  void assign(const T& t)
  {
    clear();
    p = &t;
  }

  template<class T>
  void assign(void *buf, T&& t)
  {
    clear();
    ::new (buf) T(std::forward<T>(t));
    p = buf;

    if (!std::is_trivially_destructible<T>::value)
      destroy = [](const void *p) { static_cast<const T*>(p)->~T(); };
  }

  constexpr const void *get() const noexcept { return p; }
};

class Var_ {
protected:
  struct Impl {
    Impl();
    std::string name;
    int id;
    mutable cow_buf p;
  };

  static_assert(std::is_standard_layout<Impl>::value, "Must be standard layout!");

  template<class T>
  struct with_buf {
    Impl impl;
    alignas(T) unsigned char buf[sizeof(T)];
  };

  Impl *impl = nullptr;
  friend class Query;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
  Var_(Impl *impl): impl(impl) {}

  template<class T>
  static
  Var_ mkvar(const std::string& name);
public:
  Var_(const Var_&);
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  Var_& operator=(Var_&&) = default;
  Var_() = default;
  void zap() const { impl->p.clear(); }
  int get_id() const noexcept { return impl->id; }
};

template<class T>
class Var : public Var_ {
public:
  Var(const std::string& name = std::string())
    : Var_(Var_::mkvar<T>(name))
  {}

  void assign(const T& t)
  {
    assert(!impl->p);
    impl->p.assign(t);
  }

  bool unify(const T& t) const
  {
    if (impl->p)
      return *get() == t;
    impl->p.assign(t);
    return true;
  }

  bool unify(T&& t) const
  {
    if (impl->p)
      return *get() == t;
    impl->p.assign(static_cast<void*>(((with_buf<T>*)impl)->buf), std::move(t));
    return true;
  }

  const T *get() const noexcept { return static_cast<const T*>(impl->p.get()); }
  explicit operator bool() const noexcept { return get(); }
  const T *operator->() const noexcept { return get(); }
  const T& operator*() const noexcept { return *get(); }
  friend std::ostream& operator<<(std::ostream& os, const Var& v)
  {
    os << *v.impl;
    if (v.impl->p) {
      const T& t = *v.get();
      os << "=" << t;
    }
    return os;
  }
};

class Query : public IPrint {
private:
  static Query *current;
  std::vector<Rule> rules;
  void print(std::ostream& os) const override;
  flat::autorelease pool;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);

  friend class Var_;
  std::vector<Var_::Impl*> vars;

  flat::guard with_query();

  struct cmp {
    bool operator()(Collection_base *l, Collection_base *r) const;
  };
  flat::set<Collection_base *, cmp> to_merge; // TODO: real query plan
  void configure();
public:
  template<typename F>
  Query(F&& build)
    : pool("Query")
  {
    flat::autorelease::scoped guard(pool);
    flat::guard current_query = with_query();
    build();
    configure();
  }
  Query(Query&&);
  static void print_vars(std::ostream& os, const Rule::with_vars& vs);
  void run();
};

template<class T>
Var_ Var_::mkvar(const std::string& name)
{
  auto res = Query::current->pool.template allocate<Var_::with_buf<T>>();
  Impl *impl = &(res->impl);
  impl->name = name;
  impl->id = Query::current->vars.size();
  Query::current->vars.push_back(impl);
  return impl;
}
