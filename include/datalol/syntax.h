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
#include "type_traits.h"
#include "fluid.h"

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

class dependency : public IPrint {
  friend class Query;
  virtual size_t merge(bool recursive) = 0;
};

class Collection : public dependency {
protected:
  ident id;
  Collection(Collection&&) = default;
public:
  Collection(const Collection&) = delete;
  explicit Collection(const ident& id);
  std::string get_name() const noexcept { return id.get_name(); }
  virtual Json::Value get_contents() const = 0;
};

class Var_ {
public:
  static constexpr size_t MAX_VARS = 64;
  class vars_t {
    std::bitset<MAX_VARS> vars;
  public:
    vars_t& operator|=(const vars_t& o) noexcept;
    vars_t& operator+=(const Var_& v) noexcept;
    vars_t& operator-=(const vars_t& o) noexcept;
    void reset() noexcept;
    bool test(const Var_& v) const noexcept;
    bool empty() const noexcept;
  };
protected:
  struct Impl : public IPrint {
    Impl(ident id);
    const void *p = nullptr;
    ident id;
    int nvar;
    void print_common(std::ostream& os) const;
  };

  Impl *impl;

  friend std::ostream& operator<<(std::ostream& os, const Var_& v) { return os << *v.impl; }
  Var_(Impl *impl): impl(impl) {}

  void register_var() const;

  friend class Query;

  void set(const void *p) const noexcept { impl->p = p; }

public:
  void zap() const { impl->p = nullptr; }
  Var_(const Var_&) = default;
  Var_(Var_&&) = default;
  Var_& operator=(const Var_&) = delete;
  const void *get() const noexcept { return impl->p; }
  static vars_t get_captured();
};

class Rule {
public:
  using vars_t = Var_::vars_t;
  struct elem_meta {
    vars_t produce;
    vars_t consume;
    dependency *dep;
    bool negative;
    elem_meta(const elem_meta&) = default;
    elem_meta(dependency *dep);
    elem_meta(const vars_t& produce, const vars_t& consume,
              dependency *dep = nullptr,
              bool negative = false);
    void negate_vars();
  };

  class Elem : public IPrint {
    friend class Query;
    Rule *rule_;
  protected:
    Elem *next_ = nullptr;
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
  public:
    enum delta_t {
      RECENT,
      STABLE,
      BOTH,
    };
  protected:
    using Elem::Elem;
    friend class Query;
    undo_pack undo;
    void next(bool doit) {
      if (doit) {
        next_->eval();
      }
      undo.zap();
    }
    delta_t use_delta() const noexcept {
      int d = rule().seminaive_current - idx;
      if (d < 0) return STABLE;
      if (d > 0) return BOTH;
      return RECENT;
    }
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

  size_t size() const { return last-head; }
  bool operator<(const Rule& o) const { return last < o.last; }
private:
  friend class Query;
  friend class Stubs;
  short head = 0, last = 0, start = 0;
  short seminaive_current = 0;
  static constexpr size_t MAX_ELEMS = 64;
  std::bitset<MAX_ELEMS> recursive;
  std::vector<Var_> undo_stack;
};

Rule::cursor operator<<(Rule::uhead&& h, Rule::ubody&& b);

template<typename T, typename Equal = detail::equal_to<T>>
class Var : public Var_ {
public:
  Var(const char *name = nullptr);
  Var(const Var& v): Var_(v) { register_var(); }
  Var(Var&&) = default;

  struct Impl : public Var_::Impl {
    using Var_::Impl::Impl;
    Equal eq;
    alignas(T) unsigned char buf[sizeof(T)];

    Impl(ident id, const Equal& eq)
      : Var_::Impl(id)
      , eq(eq)
    {}

    void print_value(std::ostream& os, const T& t, std::true_type) const { os << t; }
    void print_value(std::ostream& os, const T& t, std::false_type) const
    {
      os << "<" << ident::make<T>().type_name() << " @ " << static_cast<const void*>(&t) << ">";
    }
    void print(std::ostream& os) const override
    {
      print_common(os);
      if (p) {
        os << "=";
        print_value(os, *static_cast<const T*>(p), detail::is_printable<T>{});
      }
    }
  };

  bool set(const T& t) const
  {
    Var_::set(&t);
    return true;
  }
  bool set(T&& t) const
  {
    unsigned char *buf = static_cast<Impl*>(impl)->buf;
    ::new ((void*)buf) T(t);
    Var_::set(buf);
    return true;
  }
  bool match(const T& t) const
  {
    auto const& eq = static_cast<const Impl*>(impl)->eq;
    return !get() || eq(*get(), t);
  }
  bool unify(const T& t) const
  {
    return get()
      ? match(t) : set(t);
  }

  bool unify(T&& t) const
  {
    static_assert(std::is_trivially_destructible<T>::value, "Unification with complex rvalues not yet supported");
    // Will be supported in the glorious WCOJ future, though.
    return get()
      ? match(t) : set(std::move(t));
  }

  const T* get() const noexcept { return static_cast<const T*>(Var_::get()); }
  decltype(auto) operator->() const noexcept { return detail::pointer_helper<T>{}.arrow(get()); }
  decltype(auto) operator*() const noexcept { return detail::pointer_helper<T>{}.star(get()); }
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
    void print(std::ostream& os)
    {
      q->print(os);
    }
    void set_policy(execution_policy p)
    {
      q->policy = p;
    }
  };

  static fluid_var<Query> current;
private:

  debug_info *dbg;
  execution_policy policy = NESTED;
  std::vector<Rule::Elem*> elems;
  std::vector<Rule> rules;
  Rule::vars_t current_vars;
  friend class thunk_base;
  friend class Var_;

  struct stratum {
    detail::span<Rule> extent;
    std::vector<dependency*> to_merge;
  };
  std::vector<stratum> strata;

  std::vector<Var_> vars;
  std::vector<Collection*> db;

  Rule::elem_meta& get_meta(unsigned i);
  Rule::Elem& get_elem(unsigned i);

  void run_rule(Rule& r, size_t current_delta);

  detail::pool pool;

  friend class Stubs;
  friend class Rule::cursor;
  template<typename T, typename Cmp>
  friend class Var;
  friend class Collection;

  void verify_neg(const Rule::vars_t& bound, const Rule::Elem& e);
  void add_stratum(detail::span<Rule> extent);
  void stratify();
  void configure_rule(Rule& r, detail::span<int> order);
  void configure();
  void explain(const std::string& coll, const void *target);

  Query(const Query&) = delete;
  Query(Query&&) = delete;
  void run();
  void print_rule(std::ostream& os, const Rule&) const;
  void print_stratum(std::ostream& os, const stratum& s) const;
  void print(std::ostream& os) const;

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

  struct nothing {
    void externalize() const {}
  };

  template<typename Q>
  auto runit(Q&& q, control& ctrl, std::true_type) { return q(ctrl), nothing{}; }

  template<typename Q>
  auto runit(Q&& q, control& ctrl, std::false_type) { return q(ctrl); }

public:
  template<typename T, typename... Args>
  static
  auto allocate(Args&&... args) { return current->pool.template allocate<T>(std::forward<Args>(args)...); }

  Query(debug_info *dbg);

  template<typename Q>
  auto operator=(Q&& qf)
  {
    control ctrl(this);
    auto _ = current.assign(*this);
    auto res = runit(std::forward<Q>(qf), ctrl, std::is_void<decltype(qf(ctrl))>{});
    configure();
    run();
    return std::move(res).externalize();
  }
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
  thunk_base(const char *desc, const Rule::vars_t& vars);
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);

public:
  const Rule::vars_t& captured_vars() const { return vars; }
  template<typename Fun>
  static
  auto capture(const char *desc, Fun&& f) -> thunk<decltype(f())>;
};

struct thunk_tag_t {}; // FIXME: dispatch only on thunk_base?

template<typename Res>
class thunk : public thunk_base, public thunk_tag_t {
  thunk(const thunk&) = delete;
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

  friend class thunk_base;
  template<typename Fun>
  thunk(const char *desc, Fun&& fun)
    : thunk_base(desc)
    , fun(std::forward<Fun>(fun))
  {
  }

public:
  using result_t = Res;

  thunk(thunk&&) = default;

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
};

template<typename Fun>
auto thunk_base::capture(const char *desc, Fun&& f) -> thunk<decltype(f())>
{
  return { desc, std::forward<Fun>(f) };
}

template<typename S, typename T>
Rule::ubody operator==(Var<S>& v, T&& getter)
{
  return std::forward<T>(getter) == v;
}

}

#define THUNK(expr,...)                                                 \
  ::datalol::thunk_base::capture(#expr, ([=,##__VA_ARGS__]() -> decltype(auto) { return (expr); }))

#define DATALOL(query, ...) ::datalol::Query(DEBUG_INFO(query)) = [&](::datalol::Query::control& query)
