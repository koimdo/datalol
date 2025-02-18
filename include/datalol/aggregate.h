#pragma once

#include "query.h"

namespace datalol {

namespace detail {

template<typename... Ts>
struct varpack : public Selector<std::tuple<Ts...>, Var<Ts>...> {
  using super_t = Selector<std::tuple<Ts...>, Var<Ts>...>;
  using val_t = typename super_t::value_type;
  varpack(Var<Ts>&... vars)
    : super_t(build_selector<val_t>(vars...))
  {}
  void set(const val_t& values)
  {
    for_each_in_tuple([](size_t, auto& var, auto& val) { return var.set(val); },
                      super_t::sel,
                      values);
  }
};

template<typename Agg, typename VarPack>
struct aggregate_ {
  using key_t = typename VarPack::value_type;
  using state_t = typename Agg::state_t;
  using res_t = typename Agg::res_t;

  using bound_t = Var<typename std::decay<res_t>::type>;
  struct susp {
    VarPack keyvars;
    Agg agg;
    susp(VarPack&& keyvars, Agg&& agg)
      : keyvars(std::move(keyvars))
      , agg(std::move(agg))
    {
    }
    Rule::ubody operator==(bound_t& var);
  };

  struct body : public susp, public Rule::Body {
    body(susp&& args, bound_t& var)
      : susp(std::move(args))
      , Rule::Body({{}, {}, nullptr})
      , var(std::move(var))
    {
      this->keyvars.mark_vars(this->meta);
      this->meta.negate_vars();
      this->meta.produce.set(var.get_id());
    }
    bound_t var;

    std::map<key_t, state_t> state;
    void eval() override final
    {
      auto key = susp::keyvars.get_value();
      auto it = state.find(key);
      if (it == state.end()) {
        it = state.insert({key, this->agg.init()}).first;
      } else {
        this->agg.update(it->second);
      }
    }
    void flush() override final
    {
      for (auto const& kv : this->state) {
        this->keyvars.set(kv.first);
        Rule::Body::next(var.set(kv.second));
      }
    }
    void print(std::ostream& os) const override
    {
      os << this->var << " == " << this->agg << " group by " << this->keyvars;
    }
  };
};

template<typename Agg, typename VarPack>
Rule::ubody aggregate_<Agg, VarPack>::susp::operator==(aggregate_<Agg, VarPack>::bound_t& var)
{
  return Query::allocate<body>(std::move(*this), var);
}

template<typename Res>
struct Min_ {
  thunk<Res> th;
  using res_t = typename std::decay<Res>::type;
  using state_t = res_t;

  state_t init()
  {
    return th.apply();
  }

  void update(state_t& current)
  {
    current = std::min(current, th.apply());
  }

  res_t yield(const state_t& current)
  {
    return current;
  }

  friend std::ostream& operator<<(std::ostream& os, const Min_& m)
  {
    return os << "min<" << ident::make<res_t>().type_name() << ">(" << m.th << ")";
  }
};

template<typename Res>
struct Max_ {
  thunk<Res> th;
  using res_t = typename std::decay<Res>::type;
  using state_t = res_t;

  state_t init()
  {
    return th.apply();
  }

  void update(state_t& current)
  {
    current = std::max(current, th.apply());
  }

  res_t yield(const state_t& current)
  {
    return current;
  }

  friend std::ostream& operator<<(std::ostream& os, const Max_& m)
  {
    return os << "max<" << ident::make<res_t>().type_name() << ">(" << m.th << ")";
  }
};

}

template<typename Res>
auto min(thunk<Res>&& th)
{
  return detail::Min_<Res>{std::move(th)};
}

template<typename Res>
auto max(thunk<Res>&& th)
{
  return detail::Max_<Res>{std::move(th)};
}

template<typename Agg, typename... Vars>
auto aggregate(Agg agg, Var<Vars>&... keycols)
{
  return typename detail::aggregate_<Agg, detail::varpack<Vars...>>::susp(detail::varpack<Vars...>(keycols...), std::move(agg));
}

}
