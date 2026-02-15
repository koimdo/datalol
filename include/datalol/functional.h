// -*- C++ -*-
#pragma once

#include "selector.h"

namespace datalol {

namespace detail {

template<typename Res>
class thunk;

class thunk_base {
  std::string desc;
  vars_t vars;

protected:
  Rule::elem_meta get_meta() const noexcept
  {
    return { {}, vars };
  }
  explicit thunk_base(std::string&& desc);
  thunk_base(std::string&& desc, const vars_t& vars);
  friend std::ostream& operator<<(std::ostream& os, const thunk_base& t);

public:
  const vars_t& captured_vars() const { return vars; }
  template<typename Fun>
  static
  auto capture(std::string&& desc, Fun&& f) -> thunk<decltype(f())>;
};

template<typename Res>
class thunk : public thunk_base {
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
  thunk(std::string&& desc, Fun&& fun)
    : thunk_base(std::move(desc))
    , fun(std::forward<Fun>(fun))
  {
  }

public:
  using result_t = Res;

  thunk(thunk&&) = default;

  Res apply() const { return fun(); }

  operator Rule::uhead() &&
  {
    return Query::allocate<head>(std::move(*this));
  }

  operator Rule::ubody() &&
  {
    static_assert(detail::is_contextual_bool<Res>::value,
                  "not contextually convertible to bool!");
    return Query::allocate<guard>(std::move(*this));
  }

  template<typename AVar>
  Rule::ubody operator==(AVar&) &&;
};

template<typename Fun>
auto thunk_base::capture(std::string&& desc, Fun&& f) -> thunk<decltype(f())>
{
  return { std::move(desc), std::forward<Fun>(f) };
}

template<typename Fun, typename V>
struct binder_base : public Rule::Body {
  static_assert(std::is_base_of<thunk_base, Fun>::value, "Must be a proper thunk");
  using bound_t = V;
  using thunk_t = Fun;
  thunk_t fun;
  bound_t var;
  binder_base(thunk_t&& fun, bound_t& var)
    : Rule::Body({{}, fun.captured_vars(), nullptr})
    , fun(std::move(fun))
    , var(std::move(var))
  {
    meta.produce += var;
    meta.produce -= meta.consume;     // In `i == $_(i->lol)`, we don't actually bind `i`
  }
};

template<typename Fun, typename V>
struct binder : public binder_base<Fun, V> {
  using binder_base<Fun, V>::binder_base;
  void eval() override final
  {
    auto&& res = this->fun.apply(); // `res` is now alive for the rest of the call chain
    Rule::Body::next(this->var.unify(res));
  }
  void print(std::ostream& os) const override final
  {
    os << this->var << " == " << this->fun;
  }
};

template<typename Coll>
decltype(auto) get_first(const Coll& c)
{
  using std::begin;
  return *begin(c);
}

template<typename Res>
struct iterate_ {
  using element_t = std::decay_t<decltype(get_first(std::declval<Res>()))>;
  using binder_t = binder_base<thunk<Res>, Var<element_t>>;
  struct body : public binder_t {
    using binder_t::binder_base;
    void eval() override final
    {
      auto&& coll = this->fun.apply(); // `coll` is now alive for the rest of the call chain
      for (auto&& val : coll)
        Rule::Body::next(this->var.set(val));
    }
    void print(std::ostream& os) const override final
    {
      os << this->var << " == iterate(" << this->fun << ")";
    }
  };

  using thunk_t = typename body::thunk_t;
  thunk_t th;
  iterate_(thunk_t&& th)
    : th(std::move(th))
  {}

  Rule::ubody operator==(typename body::bound_t& var)
  {
    return Query::allocate<body>(std::move(th), var);
  }
};

template<typename Res>
struct enumerate_ {
  using element_t = std::decay_t<decltype(get_first(std::declval<Res>()))>;
  using binder_t = binder_base<thunk<Res>, tie_<Var<int>, Var<element_t>>>;
  struct body : public binder_t {
    using binder_t::binder_base;
    void eval() override final
    {
      auto&& coll = this->fun.apply(); // `coll` is now alive for the rest of the call chain
      int i=0;
      for (auto&& val : coll) {
        Rule::Body::next(this->var.set(std::tie(i, val)));
        i++;
      }
    }
    void print(std::ostream& os) const override final
    {
      os << this->var << " == enumerate(" << this->fun << ")";
    }
  };

  using thunk_t = typename body::thunk_t;
  thunk_t th;
  enumerate_(thunk_t&& th)
    : th(std::move(th))
  {}
  Rule::ubody operator==(typename body::bound_t& var)
  {
    return Query::allocate<body>(std::move(th), var);
  }
};

template<typename Res> template<typename AVar>
Rule::ubody thunk<Res>::operator==(AVar& var) &&
{
  static_assert(std::is_base_of<Var_, AVar>::value, "Must bind to variable");
  return Query::allocate<binder<thunk<Res>, AVar>>(std::move(*this), var);
}

}

template<typename Res>
auto iterate(detail::thunk<Res>&& t)
{
  return detail::iterate_<Res>(std::move(t));
}

template<typename Res>
auto enumerate(detail::thunk<Res>&& t)
{
  return detail::enumerate_<Res>(std::move(t));
}

}

#define THUNK(expr,...)                                                 \
  ::datalol::detail::thunk_base::capture(#expr, ([=,##__VA_ARGS__]() -> decltype(auto) { return (expr); }))
