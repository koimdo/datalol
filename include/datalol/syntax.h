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

template<class T, size_t MAX_SIZE>
//using static_stack = std::vector<T>;
struct static_stack {
  static_assert(std::is_trivially_destructible<T>::value, "Must be trivially destructible");
  static_assert(std::is_trivially_move_constructible<T>::value, "Must be trivially movable");

  alignas(T) unsigned char buf[MAX_SIZE*sizeof(T)];
  size_t nitems = 0;

  template<typename... Args>
  T& emplace_back(Args&&... args) {
    assert(nitems != MAX_SIZE);
    void *p = end();
    new (p) T(std::forward<Args>(args)...);
    nitems++;
    return *static_cast<T*>(p);
  }

  T& back() noexcept {
    assert(nitems);
    return *(begin() + nitems - 1);
  }

  const T& back() const noexcept {
    assert(nitems);
    return *(begin() + nitems - 1);
  }

  T& operator[](size_t n) {
    assert(n < nitems);
    return *(begin()+n);
  }

  const T& operator[](size_t n) const {
    assert(n < nitems);
    return *(begin()+n);
  }

  T *begin() noexcept { return reinterpret_cast<T*>(buf); }
  T *end()  noexcept { return begin() + nitems; }
  const T *begin() const noexcept { return reinterpret_cast<const T*>(buf); }
  const T *end() const noexcept { return begin() + nitems; }

  size_t size() const noexcept { return nitems; }
};

class Collection_base;
class Var_;
class Rule {
public:
  static constexpr size_t MAX_VARS = 64;
  typedef std::bitset<MAX_VARS> vars_t;
  struct with_vars {
    with_vars(const vars_t& positive, nullptr_t) noexcept;
    with_vars(nullptr_t, const vars_t& negative) noexcept;
    vars_t positive, negative;

    template<typename Make>
    static
    auto capture(Make&& make) -> std::pair<decltype(make()), Rule::vars_t>
    {
      Rule::vars_t vars;
      flat::guard vars_guard = capture_helper(&vars);
      auto m = make();
      return {std::move(m), std::move(vars)};
    }
  private:
    static
    flat::guard capture_helper(Rule::vars_t *dst);
  };

  struct Elem : IPrint {
    typedef void (*eval_t)(Elem&, Rule&, size_t);
    eval_t eval_ = nullptr;
    Elem(eval_t eval_);
    Elem(const Elem&) = delete;
    void eval(Rule& r, size_t idx) { (*eval_)(*this, r, idx); }
    virtual Collection_base *collection() const { return nullptr; }
  };

  struct Body : Elem {
    using Elem::Elem;
    Elem *next = nullptr;
    virtual void add_undo(Var_*) { assert(false && "Must implement add_undo() if it has positive vars"); }
  };

  struct Head : Elem {
    using Elem::Elem;
  };

  struct elem_meta {
    with_vars vars;
    Collection_base *collection;
    elem_meta(const elem_meta&) = default;
  };


  using uelem = flat::pool_ptr<Elem>;
  using uhead = flat::pool_ptr<Head>;
  using ubody = flat::pool_ptr<Body>;

  struct susp_Body {
    virtual std::pair<elem_meta, ubody> apply_Body() = 0;
  };

  struct susp_Head {
    virtual std::pair<elem_meta, uhead> apply_Head() = 0;
  };

  friend class Query;

  size_t seminaive_current = 0;     // FIXME: finer choice of Delta'd relation

  class cursor {
    friend cursor operator<<(susp_Head&& h, susp_Body&& b);
    friend cursor&& operator&(cursor&& r, susp_Body&& b);
    cursor(susp_Head&& h, susp_Body&& b);
    void append(susp_Body&& b);

    Rule *r;
  public:

    ~cursor();
  };

private:
  unsigned head = 0, last = 0;
};

Rule::cursor operator<<(Rule::susp_Head&& h, Rule::susp_Body&& b);
Rule::cursor&& operator&(Rule::cursor&& r, Rule::susp_Body&& b);

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

  static
  void register_var(const Var_*);

public:
  Var_(const Var_&) = default;
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  void zap() const { impl->p.clear(); }
  int get_id() const noexcept { return impl->id; }
};

template<class T>
class Var : public Var_ {
public:
  Var(const std::string& name = std::string())
    : Var_(Var_::mkvar<T>(name))
  {}

  Var(const Var& v): Var_(v) { register_var(this); }
  Var(Var&&) = default;

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
  static constexpr size_t MAX_ELEMS = 128;
  static Query *current;

  static_stack<std::pair<Rule::elem_meta, Rule::uelem>, MAX_ELEMS> elems;
  static_stack<Rule, MAX_ELEMS> rules;
  std::bitset<MAX_ELEMS> recursive;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::uelem get_elem(unsigned i);

  void add_elem(const Rule::elem_meta& meta, const Rule::uelem& e);
  Rule *start_rule();
  void end_rule(Rule *r);
  void run_rule(Rule& r, size_t current_delta);
  void print(std::ostream& os) const override final;
  flat::autorelease pool;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);

  friend class Var_;
  friend class Rule::cursor;
  static_stack<Var_, Rule::MAX_VARS> vars;

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
  Query::current->vars.emplace_back(Var_(impl));
  return impl;
}

template<class Res>
struct thunk {
  // TODO: move-only function
  using fun_t = std::function<Res()>;
  using result_t = Res;
  fun_t fun;
  std::string desc;

  Res apply() const { return fun(); }

  template<typename Fun>
  thunk(const std::string& desc, Fun&& fun)
    : desc(desc)
    , fun(std::move(fun))
  {}
};

template<class F>
auto make_thunk(const std::string& desc, F&& fun) -> thunk<decltype(fun())>
{
  return thunk<decltype(fun())>(desc, std::move(fun));
}
