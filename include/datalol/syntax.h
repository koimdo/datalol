// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
#include <bitset>
#include <string>
#include <functional>
#include <flat/memory>
#include <flat/span>
#include <flat/set>
#include <flat/map>
#include "debug.h"
#include <json/json.h>

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  virtual Json::Value to_json() const; // Return string representation
  friend std::ostream& operator<<(std::ostream& os, const IPrint& p) { return p.print(os), os; }
};

struct ident {
  const char *name;             // May be null
  const char *type;             // dumb raw form of some __PRETTY_F

  template<class T>
  static ident make(const char *name = nullptr)
  {
    ident res;
    res.name = name;
    res.type = __PRETTY_FUNCTION__;
    return res;
  }
  std::string type_name() const noexcept;
  std::string get_name() const;
};
std::ostream& operator<<(std::ostream& os, const ident& id);

class Collection_base : public IPrint {
protected:
  ident id;
  Collection_base(Collection_base&&) = default;
public:
  Collection_base(const Collection_base&) = delete;
  explicit Collection_base(const ident& id)
    : id(id)
  {}
  std::string get_name() const noexcept { return id.get_name(); }
  virtual size_t merge() = 0;
  virtual Json::Value get_contents() const = 0; // FIXME: just use IPrint::to_json()?
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

  template<class T>
  using with_meta = std::pair<elem_meta, flat::pool_ptr<T>>;

  class cursor {
    friend cursor operator<<(with_meta<Head>&& h, with_meta<Body>&& b);
    cursor(with_meta<Head>&& h, with_meta<Body>&& b);
    void append(with_meta<Body>&& b);

    Rule *r;
  public:
    cursor& operator&(with_meta<Body>&& b);
    ~cursor();
  };

  bool use_delta() const noexcept { return seminaive_current == idx; }
private:
  friend class Query;
  friend class Stubs;
  unsigned head = 0, last = 0;
  unsigned seminaive_current = 0;     // FIXME: finer choice of Delta'd relation
  unsigned idx;
};

Rule::cursor operator<<(Rule::with_meta<Rule::Head>&& h, Rule::with_meta<Rule::Body>&& b);

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
    ident id;
    int nvar;
    mutable cow_buf p;
  };

  static_assert(std::is_standard_layout<Impl>::value, "Must be standard layout!");

  Impl *impl;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
  Var_(Impl *impl): impl(impl) {}

  static
  void register_var(const Var_*);

  friend class Query;
public:
  Var_(const Var_&) = default;
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  void zap() const { impl->p.clear(); }
  int get_id() const noexcept { return impl->nvar; }
};

template<class T>
class Var : public Var_ {
public:
  Var(const char *name = nullptr);
  Var(const Var& v): Var_(v) { register_var(this); }
  Var(Var&&) = default;

  struct Impl : public Var_::Impl {
    alignas(T) unsigned char buf[sizeof(T)];
  };

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
    impl->p.assign(static_cast<Impl*>(impl)->buf, std::move(t));
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

  debug_info *dbg;
  std::vector<Rule::with_meta<Rule::Elem>> elems;
  std::vector<Rule> rules;
  std::bitset<MAX_ELEMS> recursive;

  // TODO: add typeid for verification on vars, db
  std::vector<Var_> vars;
  std::vector<flat::pool_ptr<Collection_base>> db;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::uelem get_elem(unsigned i);

  void run_rule(Rule& r, size_t current_delta);

  flat::autorelease pool;
  flat::guard current_query;

  friend class Stubs;
  friend class Rule::cursor;
  template<class T>
  friend class Var;

  struct cmp {
    bool operator()(Collection_base *l, Collection_base *r) const;
  };

  flat::set<Collection_base *, cmp> to_merge; // TODO: real query plan
  void configure();
  void explain(const std::string& coll, const void *target);

  Query(const Query&) = delete;
  Query(Query&&) = delete;
  void print_vars(std::ostream& os, const Rule::with_vars& vs) const;
  void run();
  void print(std::ostream& os) const;

  class iter {
    Query* q;
  public:
    iter(Query *q): q(q) {}
    bool operator!=(const iter& o) const { return q != o.q; }
    std::false_type operator*() const { return std::false_type{}; }
    void operator++();
  };

  void add_elem(const Rule::elem_meta& meta, const Rule::uelem& e);
  Rule *start_rule();
  void end_rule(Rule *r);

  template<class T>
  Var_ mkvar(const char *name)
  {
    auto buf = pool.allocate<typename Var<T>::Impl>();
    Var_::Impl *impl = buf.get(flat::unsafe_extract_pointer{});
    impl->id = ident::make<T>(name);
    impl->nvar = vars.size();
    vars.push_back(Var_(impl));
    return impl;
  }
public:
  template<typename T, typename... Args>
  static
  flat::pool_ptr<T> allocate(Args&&... args) { return current->pool.allocate<T>(std::forward<Args>(args)...); }

  Query(debug_info *dbg, const char *name);
  iter begin() { return iter{this}; }
  iter end() const { return iter{nullptr}; }
};

template<class T>
Var<T>::Var(const char *name)
  : Var_(Query::current->mkvar<T>(name))
{}

class thunk_base {
  Rule::vars_t vars;
  const char *desc;

public:
  thunk_base(const char *desc, const Rule::vars_t& vars);
  const Rule::vars_t& captured() const noexcept;
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);
};

namespace detail {
  template<class T, typename = void>
  struct is_contextual_bool : std::false_type {};
  template<class T>
  struct is_contextual_bool<T, decltype(void(std::declval<T>() ? true : false))> : std::true_type {};
}

// TODO: perhaps just wrap std::function?
template<typename Res>
class thunk : public thunk_base {
  using fun_t = std::function<Res()>;
  fun_t fun;

  struct head : Rule::Head {
    thunk fun;
    head(thunk&& th)
      : Rule::Head(eval_head)
      , fun(std::move(th))
    {}
    static void eval_head(Rule::Elem& self) { (void)static_cast<head&>(self).fun.apply(); }
    void print(std::ostream& os) const override final { os << fun; }
  };

  struct guard : Rule::Body {
    thunk fun;
    guard(thunk&& th)
      : Rule::Body(eval_body)
      , fun(std::move(th))
    {}
    static void eval_body(Rule::Elem& self_)
    {
      guard& self = static_cast<guard&>(self_);
      if (self.fun.apply()) self.next();
    }
    void print(std::ostream& os) const override final { os << fun;; }
  };

  struct binder : Rule::Body {
    thunk fun;
    using bound_t = Var<Res>;
    bound_t var;
    bool bound = false;
    binder(thunk&& fun, bound_t& var)
      : Rule::Body(eval)
      , fun(std::move(fun))
      , var(std::move(var)) {}
    static void eval(Rule::Elem& self_)
    {
      binder& self = static_cast<binder&>(self_);
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

  static
  Rule::elem_meta meta(const Rule::vars_t& vars) noexcept { return { Rule::with_vars(nullptr, vars), nullptr }; }

  template<class Fun>
  thunk(const char *desc, const Rule::vars_t& vars, Fun&& fun)
    : thunk_base(desc, vars)
    , fun(std::forward<Fun>(fun))
  {
  }

public:
  Res apply() const { return fun(); }

  template<class Fun>
  thunk(const char *desc, std::pair<Fun, Rule::vars_t>&& fvars)
    : thunk_base(desc, fvars.second)
    , fun(std::forward<Fun>(fvars.first))
  {
  }

  operator Rule::with_meta<Rule::Head>()
  {
    return std::make_pair(meta(captured()), Query::allocate<head>(std::move(*this)));
  }

  operator Rule::with_meta<Rule::Body>()
  {
    static_assert(detail::is_contextual_bool<Res>::value,
                  "not contextually convertible to bool!");
    return std::make_pair(meta(captured()), Query::allocate<head>(std::move(*this)));
  }

  Rule::with_meta<Rule::Body> operator==(Var<Res>& var)
  {
    Rule::vars_t positive, negative = captured();
    positive.set(var.get_id());
    positive &= ~negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
    Rule::elem_meta meta = { Rule::with_vars(positive, negative), nullptr };
    auto p = Query::allocate<binder>(std::move(*this), var);
    return std::make_pair(meta, p);
  }
};

template<typename T>
Rule::with_meta<Rule::Body> operator==(Var<T>& v, thunk<T>&& getter)
{
  return getter == v;
}

#define THUNK(expr,...)                                                 \
  thunk<decltype(expr)>(#expr, Rule::with_vars::capture([&]() {    \
    return ([=,##__VA_ARGS__]() -> decltype(expr) { return (expr); } ); \
  }))

#define DATALOL(query, ...) for (auto query : ::Query(DEBUG_INFO(), #query, ##__VA_ARGS__))
