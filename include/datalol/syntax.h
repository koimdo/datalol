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

class type_id_t {
  const char *type;
  constexpr type_id_t(const char *type): type(type) {}
public:
  template<typename T>
  static type_id_t of() { return type_id_t{__PRETTY_FUNCTION__}; }
  bool operator==(type_id_t o) const { return !strcmp(type, o.type); }
  bool operator!=(type_id_t o) const { return strcmp(type, o.type); }
  bool operator<(type_id_t o) const { return strcmp(type, o.type) < 0; }

  std::string type_name() const;

  void assert_eq(type_id_t o) const
  {
    if (*this != o) {
      std::cerr << "Type mismatch: " << type_name() << " and " << o.type_name() << "\n";
      assert(false);
    }
  }
};

struct ident {
  type_id_t type;
  const char *name;             // May be null
  template<class T>
  static ident make(const char *name = nullptr)
  {
    return ident{type_id_t::of<T>(), name};
  }
  std::string type_name() const { return type.type_name(); }
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

class Var_ {
public:
  static constexpr size_t MAX_VARS = 64;
  typedef std::bitset<MAX_VARS> vars_t;
protected:
  struct Impl {
    Impl(ident id);
    ~Impl();
    void clear();
    const void *p = nullptr;
    mutable void (*destroy)(const void *) = nullptr;
    ident id;
    int nvar;
  };

  static_assert(std::is_standard_layout<Impl>::value, "Must be standard layout!");

  Impl *impl;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
  Var_(Impl *impl): impl(impl) {}

  friend class thunk_base;
  static vars_t *current_vars;

  static
  void register_var(const Var_*);

  friend class Query;
public:
  Var_(const Var_&) = default;
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  void zap() const { impl->clear(); }
  int get_id() const noexcept { return impl->nvar; }
};

class Rule {
public:
  using vars_t = Var_::vars_t;
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
    Var_ *undo_vars;
    unsigned undo_count;
    friend class Query;
  protected:
    void next(bool doit) {
      if (doit) {
        ++rule().idx;
        next_->eval();
      }
      for (unsigned i=0; i<undo_count; ++i)
        undo_vars[i].zap();
    }
  };

  class Head : public Elem {
    using Elem::Elem;
  };

  struct elem_meta {
    vars_t positive, negative;
    Collection_base *collection;
    elem_meta(const elem_meta&) = default;
  };

  template<class T>
  using with_meta = std::pair<elem_meta, flat::pool_ptr<T>>;

  class cursor {
    friend cursor operator<<(with_meta<Head>&& h, with_meta<Body>&& b);
    cursor(with_meta<Head>&& h, with_meta<Body>&& b);

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
  std::vector<Var_> undo_stack;
};

Rule::cursor operator<<(Rule::with_meta<Rule::Head>&& h, Rule::with_meta<Rule::Body>&& b);

template<class T>
class Var : public Var_ {
public:
  Var(const char *name = nullptr);
  Var(const Var& v): Var_(v) { register_var(this); }
  Var(Var&&) = default;

  struct Impl : public Var_::Impl {
    using Var_::Impl::Impl;
    alignas(T) unsigned char buf[sizeof(T)];
  };

  bool unify(const T& t) const
  {
    if (impl->p)
      return *get() == t;
    impl->clear();
    impl->p = &t;
    return true;
  }

  bool unify(T&& t) const
  {
    if (impl->p)
      return *get() == t;

    impl->clear();
    impl->p = ::new (static_cast<Impl*>(impl)->buf) T(std::forward<T>(t));
    if (!std::is_trivially_destructible<T>::value)
      impl->destroy = [](const void *p) { static_cast<const T*>(p)->~T(); };
    return true;
  }

  const T *get() const noexcept
  {
    const T *res = static_cast<const T*>(impl->p);
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
public:
  class control {
    friend class Query;
  protected:
    Query* q;
    control(Query *q): q(q) {}
  public:
    // TODO: public configuration methods for query
  };

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
  Rule::Elem& get_elem(unsigned i);

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
  void print_vars(std::ostream& os, const Rule::elem_meta& vs) const;
  void run();
  void print(std::ostream& os) const;

  struct iter : public control {
    using control::control;
    bool operator!=(const iter& o) const { return q != o.q; }
    control operator*() { return *this; }
    void operator++();
  };

  void add_elem(const Rule::with_meta<Rule::Elem>& e);
  Rule *start_rule();
  void end_rule(Rule *r);

  template<class T>
  Var_ mkvar(const char *name)
  {
    auto buf = pool.allocate<typename Var<T>::Impl>(ident::make<T>(name));
    Var_::Impl *impl = buf.get(flat::unsafe_extract_pointer{});
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

template<typename Res>
class thunk;

class thunk_base {
  Rule::vars_t vars;
  const char *desc;

protected:
  Rule::elem_meta get_meta() const noexcept
  {
    return { {}, vars, nullptr };
  }
  explicit thunk_base(const char *desc);
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);

public:
  template<typename Make>
  static
  auto capture(const char *desc, Make&& make) -> thunk<decltype(make()())>;
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
      self.next(self.fun.apply() && true);
    }
    void print(std::ostream& os) const override final { os << fun;; }
  };

  struct binder : Rule::Body {
    thunk fun;
    using bound_t = Var<Res>;
    bound_t var;
    binder(thunk&& fun, bound_t& var)
      : Rule::Body(eval)
      , fun(std::move(fun))
      , var(std::move(var)) {}
    static void eval(Rule::Elem& self_)
    {
      binder& self = static_cast<binder&>(self_);
      self.next(self.var.unify(self.fun.apply()));
    }
    void print(std::ostream& os) const override final
    {
      bound_t::do_print(os, var) << " == " << fun;
    }
  };

  friend class thunk_base;
  template<class Fun>
  thunk(const char *desc, Fun&& fun)
    : thunk_base(desc)
    , fun(std::forward<Fun>(fun))
  {
  }

public:
  Res apply() const { return fun(); }

  operator Rule::with_meta<Rule::Head>()
  {
    return std::make_pair(get_meta(), Query::allocate<head>(std::move(*this)));
  }

  operator Rule::with_meta<Rule::Body>()
  {
    static_assert(detail::is_contextual_bool<Res>::value,
                  "not contextually convertible to bool!");
    return std::make_pair(get_meta(), Query::allocate<head>(std::move(*this)));
  }

  Rule::with_meta<Rule::Body> operator==(Var<Res>& var)
  {
    auto meta = get_meta();
    meta.positive.set(var.get_id());
    meta.positive &= ~meta.negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
    return std::make_pair(std::move(meta), Query::allocate<binder>(std::move(*this), var));
  }
};

template<typename Make>
auto thunk_base::capture(const char *desc, Make&& make) -> thunk<decltype(make()())>
{
  Rule::vars_t vars;
  assert(!Var_::current_vars);        // No nested lambdas
  Var_::current_vars = &vars;
  return { desc, make() };
}

template<typename T>
Rule::with_meta<Rule::Body> operator==(Var<T>& v, thunk<T>&& getter)
{
  return getter == v;
}

#define THUNK(expr,...)                                                 \
  thunk_base::capture(#expr, [&]() {                                    \
    return ([=,##__VA_ARGS__]() -> decltype(expr) { return (expr); } ); \
  })

#define DATALOL(query, ...) for (auto query : ::Query(DEBUG_INFO(), #query, ##__VA_ARGS__))
