// -*- C++ -*-
#pragma once

#include <cassert>
#include <vector>
#include <iostream>
#include <bitset>
#include <functional>
#include <json/json.h>

#include "relation.h"
#include "debug.h"
#include "pool.h"

namespace datalol {

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
  bool operator==(type_id_t o) const;
  bool operator!=(type_id_t o) const;
  bool operator<(type_id_t o) const;

  std::string type_name() const;
};

class ident {
  type_id_t type;
  const char *name;             // May be null
  constexpr ident(type_id_t type, const char *name): type(type), name(name) {}
public:
  template<typename T>
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
  explicit Collection_base(const ident& id);
  std::string get_name() const noexcept { return id.get_name(); }
  virtual size_t merge() = 0;
  virtual Json::Value get_contents() const = 0;
};

class Var_ {
public:
  static constexpr size_t MAX_VARS = 64;
  typedef std::bitset<MAX_VARS> vars_t;
protected:
  struct Impl {
    Impl(ident id);
    const void *p = nullptr;
    ident id;
    int nvar;
  };

  Impl *impl;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
  Var_(Impl *impl): impl(impl) {}

  friend class thunk_base;
  static vars_t *current_vars;

  static
  void register_var(const Var_*);

  friend class Query;

  void set(const void *p) const { impl->p = p; }

public:
  void zap() const { impl->p = nullptr; }
  Var_(const Var_&) = default;
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  const void *get() const { return impl->p; }
  int get_id() const noexcept { return impl->nvar; }
};

class Rule {
public:
  using vars_t = Var_::vars_t;
  struct elem_meta {
    vars_t produce;
    vars_t consume;
    Collection_base *collection;
    bool negative;
    elem_meta(const elem_meta&) = default;
    elem_meta(Collection_base *collection);
    elem_meta(const vars_t& produce, const vars_t& consume,
              Collection_base *collection = nullptr,
              bool negative = false)
      : produce(produce), consume(consume)
      , collection(collection)
      , negative(negative)
    {}
    void negate_vars();
  };

  class Elem : public IPrint {
    friend class Query;
    Rule *rule_;
  protected:
    int idx;
    elem_meta meta;
    Elem(const elem_meta& m);
    Elem(const Elem&) = delete;
    Rule& rule() const noexcept { return *rule_; }
  public:
    virtual void configure();
    virtual void eval() = 0;
  };

  struct undo_pack {
    Var_ *vars;
    unsigned count;
    void zap() const
    {
      for (unsigned i=0; i<count; ++i)
        vars[i].zap();
    }
  };
  class Body : public Elem {
    using Elem::Elem;
    Elem *next_ = nullptr;
    friend class Query;

  protected:
    undo_pack undo;
    void next(bool doit) {
      if (doit) {
        next_->eval();
      }
      undo.zap();
    }
    bool use_delta() const noexcept { return rule().seminaive_current - idx; }
  };

  class Head : public Elem {
    using Elem::Elem;
  };

  using ubody = detail::pool::wrap<Body>;
  using uhead = detail::pool::wrap<Head>;

  class cursor {
    friend cursor operator<<(uhead&& h, ubody&& b);
    cursor(uhead&& h, ubody&& b);

    Rule *r;
  public:
    cursor& operator&(Rule::ubody&& b);
    ~cursor();
  };
private:
  friend class Query;
  friend class Stubs;
  unsigned head = 0, last = 0;
  unsigned start = 0;
  int seminaive_current = 0;
  std::vector<Var_> undo_stack;
};

Rule::cursor operator<<(Rule::uhead&& h, Rule::ubody&& b);

template<typename T, typename Compare = std::less<T>>
class Var : public Var_ {
public:
  Var(const char *name = nullptr);
  Var(const Var& v): Var_(v) { register_var(this); }
  Var(Var&&) = default;

  struct Impl : public Var_::Impl {
    using Var_::Impl::Impl;
    Compare cmp;
    Impl(ident id, const Compare& cmp)
      : Var_::Impl(id)
      , cmp(cmp)
    {}
  };

  bool unify(const T& t) const
  {
    auto const& cmp = static_cast<const Impl*>(impl)->cmp;
    return get()
      // FIXME: use eq-like comaprison instead of this inefficient incantation
      ? !cmp(*get(), t) && !cmp(t, *get())
      : (set(&t), true);
  }

  const T *get() const noexcept
  {
    const T *res = static_cast<const T*>(Var_::get());
    return res;
  }
  const T *operator->() const noexcept { return get(); }
  const T& operator*() const noexcept { return *get(); }

  std::ostream& print(std::ostream& os) const
  {
    os << *impl;
    if (get()) {
      os << "=" << *get();
    }
    return os;
  }
};

class Query {
public:
  enum execution_policy {
    NESTED,
    // TODO: WCOJ
  };
  class control {
    friend class Query;
  protected:
    Query* q;
    control(Query *q): q(q) {}
  public:
    void manual_stratify(std::initializer_list<unsigned> counts);
    void set_policy(execution_policy p)
    {
      q->policy = p;
    }
  };

private:
  static constexpr size_t MAX_ELEMS = 128;
  static Query *current;

  const char *name;
  debug_info *dbg;
  Query *old_current;
  execution_policy policy = NESTED;
  std::vector<Rule::Elem*> elems;
  std::vector<Rule> rules;

  struct stratum {
    detail::span<Rule> extent;
    std::vector<Collection_base*> to_merge;
  };
  std::vector<stratum> strata;
  std::bitset<MAX_ELEMS> recursive;

  std::vector<Var_> vars;
  std::vector<Collection_base*> db;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::Elem& get_elem(unsigned i);

  void run_rule(Rule& r, size_t current_delta);

  detail::pool pool;

  friend class Stubs;
  friend class Rule::cursor;
  template<typename T, typename Cmp>
  friend class Var;
  friend class Collection_base;

  void verify_neg(const Rule::vars_t& bound, const Rule::Elem& e);
  void add_stratum(detail::span<Rule> extent);
  void stratify();
  void configure_rule(Rule& r, detail::span<int> order);
  void configure();
  void explain(const std::string& coll, const void *target);

  Query(const Query&) = delete;
  Query(Query&&) = delete;
  void print_vars(std::ostream& os, const Rule::elem_meta& vs) const;
  void run();
  void print_rule(std::ostream& os, const Rule&) const;
  void print(std::ostream& os) const;

  struct iter : public control {
    using control::control;
    bool operator!=(const iter& o) const { return q != o.q; }
    control operator*() { return *this; }
    void operator++();
  };

  void add_elem(Rule::Elem *e);
  Rule *start_rule();
  void end_rule(Rule *r);

  template<typename T>
  Var_ mkvar(const char *name)
  {
    Var_::Impl *impl = pool.allocate<typename Var<T>::Impl>(ident::make<T>(name)).get();
    impl->nvar = vars.size();
    vars.push_back(Var_(impl));
    return impl;
  }
public:
  template<typename T, typename... Args>
  static
  auto allocate(Args&&... args) { return current->pool.template allocate<T>(std::forward<Args>(args)...); }

  Query(debug_info *dbg, const char *name);
  ~Query();
  iter begin() { return iter{this}; }
  iter end() const { return iter{nullptr}; }
};

template<typename T, typename Cmp>
Var<T, Cmp>::Var(const char *name)
  : Var_(Query::current->mkvar<T>(name))
{
  static_assert(sizeof(Var<T, Cmp>) == sizeof(Var_), "Extra members?");
}

template<typename Res>
class thunk;

class thunk_base {
  Rule::vars_t vars;
  const char *desc;

protected:
  Rule::elem_meta get_meta() const noexcept
  {
    return { {}, vars };
  }
  explicit thunk_base(const char *desc);
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);

public:
  template<typename Make>
  static
  auto capture(const char *desc, Make&& make) -> thunk<decltype(make()())>;
};

namespace detail {
  template<typename T, typename = void>
  struct is_contextual_bool : std::false_type {};
  template<typename T>
  struct is_contextual_bool<T, decltype(void(std::declval<T>() ? true : false))> : std::true_type {};
}

template<typename Res>
class thunk : public thunk_base {
  using fun_t = std::function<Res()>;
  fun_t fun;

  struct head : Rule::Head {
    thunk fun;
    head(thunk&& th)
      : Rule::Head(th.get_meta())
      , fun(std::move(th))
    {}
    void eval() override final { (void)fun.apply(); }
    void print(std::ostream& os) const override final { os << fun; }
  };

  struct guard : Rule::Body {
    thunk fun;
    guard(thunk&& th)
      : Rule::Body(th.get_meta())
      , fun(std::move(th))
    {}
    void eval() override final
    {
      next(fun.apply() ? true : false);
    }
    void print(std::ostream& os) const override final { os << fun;; }
  };

  struct binder : Rule::Body {
    thunk fun;
    using bound_t = Var<Res>;
    bound_t var;
    binder(thunk&& fun, bound_t& var)
      : Rule::Body(fun.get_meta())
      , fun(std::move(fun))
      , var(std::move(var))
    {
      meta.produce.set(var.get_id());
      meta.produce &= ~meta.negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
    }
    void eval() override final
    {
      decltype(fun.apply()) res = fun.apply(); // `res` is now alive for the rest of the call chain
      next(var.unify(res));
    }
    void print(std::ostream& os) const override final
    {
      var.print(os) << " == " << fun;
    }
  };

  friend class thunk_base;
  template<typename Fun>
  thunk(const char *desc, Fun&& fun)
    : thunk_base(desc)
    , fun(std::forward<Fun>(fun))
  {
  }

public:
  Res apply() const { return fun(); }

  operator Rule::uhead()
  {
    return Query::allocate<head>(std::move(*this));
  }

  operator Rule::ubody()
  {
    static_assert(detail::is_contextual_bool<Res>::value,
                  "not contextually convertible to bool!");
    return Query::allocate<guard>(std::move(*this));
  }

  Rule::ubody operator==(Var<Res>& var)
  {
    return Query::allocate<binder>(std::move(*this), var);
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
Rule::ubody operator==(Var<T>& v, thunk<T>&& getter)
{
  return getter == v;
}

}

#define THUNK(expr,...)                                                 \
  ::datalol::thunk_base::capture(#expr, [&]() {                         \
    return ([=,##__VA_ARGS__]() -> decltype(expr) { return (expr); } ); \
  })

#define DATALOL(query, ...) for (auto query : ::datalol::Query(DEBUG_INFO(), #query))
