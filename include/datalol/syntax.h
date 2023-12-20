// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
#include <bitset>
#include <string>
#include <flat/memory>
#include <flat/span>
#include <flat/set>
#include <flat/map>
#include "debug.h"

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  virtual Json::Value to_json() const { return false; };
};

class Collection_base : public IPrint {
protected:
  std::string name;

  template<typename T, typename... Args>
  static
  T& make(const std::string& name, Args&&... args);
public:
  Collection_base(const Collection_base&) = delete;
  Collection_base(const std::string& name)
    : name(name)
  {}
  const std::string& get_name() const noexcept { return name; }
  virtual size_t merge() = 0;
};

template<class type> constexpr std::string GetName()
{
  const char* start = __PRETTY_FUNCTION__;
  while(*start != '=') ++start;
  start += 2;
  size_t size = 0;
  while(start[size] != ';') ++size;
  return std::string(start, size);
}

template<class T, size_t MAX_SIZE>
//using static_stack = std::vector<T>;
class static_stack {
  static_assert(std::is_trivially_destructible<T>::value, "Must be trivially destructible");
  static_assert(std::is_trivially_move_constructible<T>::value, "Must be trivially movable");

  alignas(T) unsigned char buf[MAX_SIZE*sizeof(T)];
  size_t nitems = 0;
public:
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

class Var_;
class Rule {
public:
  static constexpr size_t MAX_VARS = 64;
  typedef std::bitset<MAX_VARS> vars_t;
  struct with_vars {
    with_vars(const vars_t& positive, nullptr_t) noexcept;
    with_vars(nullptr_t, const vars_t& negative) noexcept;
    with_vars(const vars_t& positive, const vars_t& negative) noexcept;
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

  class Elem : public IPrint {
    typedef void (*eval_t)(Elem&);
    friend class Query;
    eval_t eval_ = nullptr;
    Rule *rule_;
  protected:
    Elem(eval_t eval_);
    void set_eval(eval_t eval_);
    Elem(const Elem&) = delete;
    Rule& rule() noexcept { return *rule_; }
  public:
    void eval() { (*eval_)(*this); }
  };

  class Body : public Elem {
    using Elem::Elem;
    Elem *next_ = nullptr;
    friend class Query;
  protected:
    void next() {
      ++rule().idx;
      next_->eval();
    }
  public:
    virtual void add_undo(Var_*) { assert(false && "Must implement add_undo() if it has positive vars"); }
  };

  class Head : public Elem {
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


  class cursor {
    friend cursor operator<<(susp_Head&& h, susp_Body&& b);
    friend cursor&& operator&(cursor&& r, susp_Body&& b);
    cursor(susp_Head&& h, susp_Body&& b);
    void append(susp_Body&& b);

    Rule *r;
  public:

    ~cursor();
  };

  bool use_delta() const noexcept { return seminaive_current == idx; }
private:
  friend class Stubs;
  unsigned head = 0, last = 0;
  size_t seminaive_current = 0;     // FIXME: finer choice of Delta'd relation
  size_t idx;
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
    std::string type;
    int id;
    mutable cow_buf p;
  };

  static_assert(std::is_standard_layout<Impl>::value, "Must be standard layout!");

  template<class T>
  struct with_buf {
    Impl impl;
    alignas(T) unsigned char buf[sizeof(T)];
  };

  Impl *impl;
  friend class Query;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
  Var_(Impl *impl): impl(impl) {}

  template<class T>
  static
  Var_ mkvar(const std::string& name);

  static
  void register_var(const Var_*);

  const std::string& get_name() const noexcept { return impl->name; }

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

  const T *get() const noexcept
  {
    const T *res = static_cast<const T*>(impl->p.get());
    assert(res && "Unbound var dereferenced");
    return res;
  }
  const T *operator->() const noexcept { return get(); }
  operator const T&() const noexcept { return *get(); }
  const T& operator*() const noexcept { return *get(); }
  static std::ostream& do_print(std::ostream& os, const Var& v)
  {
    os << *v.impl;
    if (v.impl->p) {
      const T& t = *v.get();
      os << "=" << t;
    }
    return os;
  }
};

class Query {
private:
  static constexpr size_t MAX_ELEMS = 128;
  static Query *current;

  std::string name;
  debug_info *dbg;
  static_stack<std::pair<Rule::elem_meta, Rule::uelem>, MAX_ELEMS> elems;
  static_stack<Rule, MAX_ELEMS> rules;
  std::bitset<MAX_ELEMS> recursive;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::uelem get_elem(unsigned i);

  void add_elem(const Rule::elem_meta& meta, const Rule::uelem& e);
  Rule *start_rule();
  void end_rule(Rule *r);
  void run_rule(Rule& r, size_t current_delta);
  void print(std::ostream& os) const;

  flat::autorelease pool;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);

  friend class Var_;
  friend class Rule::cursor;
  friend class Collection_base;
  friend class Stubs;
  static_stack<Var_, Rule::MAX_VARS> vars;

  using guard_t = std::pair<flat::guard, flat::autorelease::scoped>;
  guard_t with_query();

  struct cmp {
    bool operator()(Collection_base *l, Collection_base *r) const;
  };

  // TODO: can be replaced by a vector, collections appear by declaration order
  // TODO: add typeid for verification
  flat::map<std::string, flat::pool_ptr<Collection_base>> db;
  flat::set<Collection_base *, cmp> to_merge; // TODO: real query plan
  void configure();
public:
  class Builder {
    Query* q;
    guard_t current_query;
    struct iter {
      Query* q;
      iter(Query *q): q(q) {}
      bool operator!=(const iter& o) const { return q != o.q; }
      std::false_type operator*() const { return std::false_type{}; }
      void operator++();
    };
  public:
    Builder(Query* q, debug_info *dbg);
    iter begin() { return iter{q}; }
    iter end() const { return iter{nullptr}; }
  };

  Query();
  static void set_title(const std::string& title)
  {
    current->name = title;
  }
  Query(Query&&);
  static void print_vars(std::ostream& os, const Rule::with_vars& vs);
  void run();
};

template<class T>
Var_ Var_::mkvar(const std::string& name)
{
  auto res = flat::allocate<Var_::with_buf<T>>();
  Impl *impl = &(res->impl);
  impl->name = name;
  impl->type = GetName<T>();
  impl->id = Query::current->vars.size();
  Query::current->vars.emplace_back(Var_(impl));
  return impl;
}

template<class T, typename... Args>
T& Collection_base::make(const std::string& name, Args&&... args)
{
  auto& db = Query::current->db;
  auto it = db.find(name);
  if (db.end() != it)
    return static_cast<T&>(*(it->second));
  auto res = flat::allocate<T>(name, std::forward<Args>(args)...);
  db.emplace(name, res);

  return *res;
}

template<typename Fun>
class thunk {
  using fun_t = Fun;
  fun_t fun;
  const char *desc;

public:
  using result_t = decltype(std::declval<Fun>()());

  result_t apply() const { return fun(); }

  thunk(const char *desc, Fun&& fun)
    : desc(desc)
    , fun(std::move(fun))
  {
  }
  friend
  std::ostream& operator<<(std::ostream& os, const thunk& t)
  {
    return os << "THUNK(" << t.desc << ")";
  }
};

template<class F>
auto make_thunk(const char *desc, F&& fun) -> thunk<F>
{
  //std::cerr << "sizeof(Thunk [" << desc << "]): " << sizeof(thunk<Res, F>) << "\n";
  return thunk<F>(desc, std::move(fun));
}

template<typename> class binder_susp;
template<typename Fun>
class thunk_susp : public Rule::susp_Head, public Rule::susp_Body {
  using thunk_t = thunk<Fun>;
  std::pair<thunk_t, Rule::vars_t> tv;

  struct elem_common {
    thunk_t fun;
    elem_common(thunk_t&& th)
      : fun(std::move(th))
    {}
    void print_(std::ostream& os) const { os << fun; }
  };

  struct head : elem_common, Rule::Head {
    head(thunk_t&& th)
      : elem_common(std::move(th))
      , Rule::Head(eval_head)
    {}
    static void eval_head(Rule::Elem& self) { (void)static_cast<head&>(self).fun.apply(); }
    void print(std::ostream& os) const override final { elem_common::print_(os); }
  };

  struct guard : elem_common, Rule::Body {
    guard(thunk_t&& fun)
      : elem_common(std::move(fun))
      , Rule::Body(eval_body)
    {}
    static void eval_body(Rule::Elem& self_)
    {
      guard& self = static_cast<guard&>(self_);
      if (self.fun.apply()) self.next();
    }
    void print(std::ostream& os) const override final { elem_common::print_(os); }
  };

  Rule::elem_meta meta() const noexcept { return { Rule::with_vars(nullptr, tv.second), nullptr }; }

  std::pair<Rule::elem_meta, Rule::uhead> apply_Head() override final
  {
    return std::make_pair(meta(), flat::allocate<head>(std::move(tv.first)));
  }

  std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
  {
    return std::make_pair(meta(), flat::allocate<guard>(std::move(tv.first)));
  }

  friend class binder_susp<Fun>;
public:
  thunk_susp(std::pair<thunk_t, Rule::vars_t>&& tt)
    : tv(std::move(tt)) {}

};

template<class F>
thunk_susp<F> make_susp(std::pair<thunk<F>, Rule::vars_t>&& tt) {
  return thunk_susp<F>(std::move(tt));
}

template<typename Fun>
class binder_susp : public Rule::susp_Body {
public:
  using thunk_t = thunk<Fun>;
  using bound_t = Var<typename thunk_t::result_t>;
  binder_susp(thunk_susp<Fun>&& ts, bound_t& bound)
    : tv(std::move(ts.tv))
    , bound(bound) {}

private:
  std::pair<thunk_t, Rule::vars_t> tv;
  bound_t& bound;

  struct Binder : Rule::Body {
    thunk_t fun;
    bound_t var;
    bool bound = false;
    Binder(thunk_t&& fun, bound_t& var)
      : Rule::Body(eval)
      , fun(std::move(fun))
      , var(std::move(var)) {}
    static void eval(Rule::Elem& self_)
    {
      Binder& self = static_cast<Binder&>(self_);
      if (self.var.unify(self.fun.apply()))
        self.next();
      if (self.bound)
        self.var.zap();
    }
    void add_undo(Var_ *v) override final
    {
      assert(!v || v->get_id() == var.get_id());
      if (v)
        bound = true;
    }
    void print(std::ostream& os) const override final
    {
      bound_t::do_print(os, var) << " == " << fun;
    }
  };
  std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
  {
    Rule::vars_t positive;
    positive.set(bound.get_id());
    positive &= ~tv.second;     // In `i == $_(i->lol)`, we don't actually bind `i`
    Rule::elem_meta meta = { Rule::with_vars(positive, tv.second), nullptr };
    auto p = flat::allocate<Binder>(std::move(tv.first), bound);
    return std::make_pair(meta, p);
  }
};

template<typename Fun>
binder_susp<Fun> operator==(thunk_susp<Fun>&& getter, typename binder_susp<Fun>::bound_t& v)
{
  return binder_susp<Fun>(std::move(getter), v);
}

template<typename Fun>
binder_susp<Fun> operator==(typename binder_susp<Fun>::bound_t& v, thunk_susp<Fun>&& getter)
{
  return binder_susp<Fun>(std::move(getter), v);
}

#define CONCAT_(a,b) a##b
#define CONCAT(a,b) CONCAT_(a,b)
#define UNIQ_(prefix) CONCAT(prefix,__LINE__)

#define THUNK(expr,...)                                                 \
  make_susp(Rule::with_vars::capture([&]() {                            \
    return make_thunk(#expr, ([=,##__VA_ARGS__]() -> decltype(expr) { return (expr); } )); \
  }))

#define DATALOL_Q(query, ...) for (auto UNIQ_(dummy) : ::Query::Builder(&query, DEBUG_INFO(), ##__VA_ARGS__))
#define DATALOL(...) ({                                                 \
      Query UNIQ_(query);                                               \
      DATALOL_Q(UNIQ_(query)) { __VA_ARGS__ }                           \
      std::move(UNIQ_(query));                                          \
    })
