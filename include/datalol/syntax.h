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

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  virtual Json::Value to_json() const { return false; };
};

struct ident {
  const char *name;             // May be null
  const char *type;             // dumb raw form of some __PRETTY_F
  int id;

  template<class T>
  static ident make(int id, const char *name = nullptr)
  {
    ident res;
    res.id = id;
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
public:
  Collection_base(const Collection_base&) = delete;
  Collection_base(const ident& id)
    : id(id)
  {}
  std::string get_name() const noexcept { return id.get_name(); }
  virtual size_t merge() = 0;
  virtual Json::Value get_contents() const = 0;
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
  friend class Query;
  friend class Builder;
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
    ident id;
    mutable cow_buf p;
  };

  static_assert(std::is_standard_layout<Impl>::value, "Must be standard layout!");

  template<class T>
  struct with_buf {
    Impl impl;
    alignas(T) unsigned char buf[sizeof(T)];
  };

  Impl *impl;
  friend class Builder;

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
  int get_id() const noexcept { return impl->id.id; }
};

template<class T>
class Var : public Var_ {
public:
  Var(const char *name = nullptr);
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

  debug_info *dbg;
  std::vector<std::pair<Rule::elem_meta, Rule::uelem>> elems;
  std::vector<Rule> rules;

  // TODO: add typeid for verification on vars, db
  std::vector<Var_> vars;
  std::vector<flat::pool_ptr<Collection_base>> db;
  std::bitset<MAX_ELEMS> recursive;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::uelem get_elem(unsigned i);

  void run_rule(Rule& r, size_t current_delta);
  void print(std::ostream& os) const;

  flat::autorelease pool;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);

  friend class Stubs;

  using guard_t = std::pair<flat::guard, flat::autorelease::scoped>;
  guard_t with_query();

  struct cmp {
    bool operator()(Collection_base *l, Collection_base *r) const;
  };

  flat::set<Collection_base *, cmp> to_merge; // TODO: real query plan
  void configure();
public:
  friend class Builder;
  Query();
  Query(Query&&);
  static void print_vars(std::ostream& os, const Rule::with_vars& vs);
  void run();
};

class Builder {
  Query* q;
  Query::guard_t current_query;
  flat::guard current_builder;
  struct iter {
    Query* q;
    iter(Query *q): q(q) {}
    bool operator!=(const iter& o) const { return q != o.q; }
    std::false_type operator*() const { return std::false_type{}; }
    void operator++();
  };

  int nvars = 0, nrels = 0;
public:
  Builder(Query* q, debug_info *dbg, const char *name = nullptr);
  iter begin() { return iter{q}; }
  iter end() const { return iter{nullptr}; }

  static Builder *current;

  void add_elem(const Rule::elem_meta& meta, const Rule::uelem& e);
  Rule *start_rule();
  void end_rule(Rule *r);

  template<class T>
  Var_ mkvar(const char *name)
  {
    if (nvars < q->vars.size())
      return q->vars[nvars++];  // FIXME: check type!
    auto buf = flat::allocate<Var_::with_buf<T>>();
    Var_::Impl *impl = &(buf->impl);
    impl->id = ident::make<T>(nvars++, name);
    q->vars.push_back(Var_(impl));
    return impl;
  }

  template<class T, typename... Args>
  T& make_rel(const char *name, Args&&... args)
  {
    std::cerr << "Collection_base::make<" << ident::make<T>(nrels, name).type_name() << ">(" << name << "): " << sizeof(T) << "\n";
    if (nrels < q->db.size())
      return static_cast<T&>(*q->db[nrels++]);
    auto res = flat::allocate<T>(ident::make<T>(nrels++, name), std::forward<Args>(args)...);
    q->db.push_back(res);

    return *res;
  }
};

template<class T>
Var<T>::Var(const char *name)
  : Var_(Builder::current->mkvar<T>(name))
{}

class thunk_base {
  Rule::vars_t vars;
  const char *desc;

public:
  thunk_base(const char *desc, const Rule::vars_t& vars);
  const Rule::vars_t& captured() const noexcept;
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);
};

// TODO: perhaps just wrap std::function?
template<typename Res>
class thunk : public thunk_base {
  using fun_t = std::function<Res()>;
  fun_t fun;

public:
  Res apply() const { return fun(); }

  template<class Fun>
  thunk(const char *desc, const Rule::vars_t vars, Fun&& fun)
    : thunk_base(desc, vars)
    , fun(std::forward<Fun>(fun))
  {
  }
};

namespace detail {
  template<class T, typename = void>
  struct is_contextual_bool : std::false_type {};
  template<class T>
  struct is_contextual_bool<T, decltype(void(std::declval<T>() ? true : false))> : std::true_type {};
}

template<typename> class binder_susp;
template<typename Res>
class thunk_susp : public Rule::susp_Head {
  using thunk_t = thunk<Res>;
  thunk_t fun;

  struct head : Rule::Head {
    thunk_t fun;
    head(thunk_t&& th)
      : Rule::Head(eval_head)
      , fun(std::move(th))
    {}
    static void eval_head(Rule::Elem& self) { (void)static_cast<head&>(self).fun.apply(); }
    void print(std::ostream& os) const override final { os << fun; }
  };

  struct guard : Rule::Body {
    thunk_t fun;
    guard(thunk_t&& th)
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

  static
  Rule::elem_meta meta(const Rule::vars_t& vars) noexcept { return { Rule::with_vars(nullptr, vars), nullptr }; }

  std::pair<Rule::elem_meta, Rule::uhead> apply_Head() override final
  {
    return std::make_pair(meta(fun.captured()), flat::allocate<head>(std::move(fun)));
  }

  struct apply_body_impl : public Rule::susp_Body {
    thunk_t fun;
    apply_body_impl(thunk_t&& fun_)
      : fun(std::move(fun_))
    {}
    std::pair<Rule::elem_meta, Rule::ubody> apply_Body() override final
    {
      return std::make_pair(meta(fun.captured()), flat::allocate<guard>(std::move(fun)));
    }
  };

  friend class binder_susp<Res>;
public:
  operator apply_body_impl()
  {
    static_assert(detail::is_contextual_bool<Res>::value,
                  "not contextually convertible to bool!");
    return apply_body_impl(std::move(fun));
  }

  template<class F>
  thunk_susp(const char *desc, std::pair<F, Rule::vars_t>&& tv)
    : fun(desc, tv.second, std::move(tv.first))
  {}
};

template<typename T>
class binder_susp : public Rule::susp_Body {
public:
  using thunk_t = thunk<T>;
  using bound_t = Var<T>;
  binder_susp(thunk_susp<T>&& ts, bound_t& bound)
    : fun(std::move(ts.fun))
    , bound(bound) {}

private:
  thunk_t fun;
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
    Rule::vars_t positive, negative = fun.captured();
    positive.set(bound.get_id());
    positive &= ~negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
    Rule::elem_meta meta = { Rule::with_vars(positive, negative), nullptr };
    auto p = flat::allocate<Binder>(std::move(fun), bound);
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
  thunk_susp<decltype(expr)>(#expr, Rule::with_vars::capture([&]() {    \
    return ([=,##__VA_ARGS__]() -> decltype(expr) { return (expr); } ); \
  }))

#define DATALOL_Q(query, ...) for (auto UNIQ_(dummy) : ::Builder(&query, DEBUG_INFO(), ##__VA_ARGS__))
#define DATALOL(...) ({                                                 \
      Query UNIQ_(query);                                               \
      DATALOL_Q(UNIQ_(query)) { __VA_ARGS__ }                           \
      std::move(UNIQ_(query));                                          \
    })
