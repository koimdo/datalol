#pragma once

#include "syntax.h"
#include "relation.h"
#include "selector.h"
#include "lattice.h"
#include "itertools.h"
#include "debug.h"

#include <cstddef>
#include <type_traits>

namespace datalol {

namespace detail {

template<typename Coll, typename T>
bool do_contains(const Coll& coll, const T& t) { return coll.contains(t); }

template<typename T>
bool do_contains(const std::vector<T>& vec, const T& t)
{
  return std::find_if(vec.begin(), vec.end(), [&t](const T& s) { return equal_to<T>{}(s, t); }) != vec.end();
}
// Backported from C++20
template<typename T>
struct remove_cvref {
  using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<typename Sel, bool has_value = true>
struct eval_helper {
  decltype(std::declval<Sel>().get_value()) value;
  eval_helper(const Sel& sel)
    : value(sel.get_value())
  {}

  template<typename T>
  static
  const T& get_elem(const T& t) { return t; }
  template<typename T>
  static
  const T& get_elem(const std::tuple<const T&>& t) { return std::get<0>(t); }

  // // TODO: SFINAE-generalize it according to support of coll.count(value)
  // template<typename T>
  // bool find_in(const std::vector<T>& vec) const
  // {
  //   return std::find(vec.begin(), vec.end(), get_elem(value)) != vec.end();
  // }
  template<typename Coll>
  bool find_in(const Coll& coll) const
  {
    return coll.contains(get_elem(value));
  }
};

template<typename Sel>
struct eval_helper<Sel, false> {
  const Sel& sel;
  eval_helper(const Sel& sel)
    : sel(sel)
  {}
  template<typename Coll>
  bool find_in(const Coll& coll) const
  {
    for (auto it = coll.iterator(); it; ++it)
      if (sel.unify(*it))
        return true;
    return false;
  }
};

template<typename Sel>
struct Matcher : public Rule::Body {
  using iterable_t = iterable<typename Sel::value_type>;
  Sel selector;
  iterable_t& origin;
  Collection& dep;

  Matcher(Sel&& sel, iterable_t& origin, Collection& dep_)
    : Rule::Body(&dep_)
    , selector(std::move(sel))
    , origin(origin)
    , dep(dep_)
  {
    sel.mark_vars(meta);
  }

  void print(std::ostream& os) const override
  {
    os << (is_negative() ? "!" : "") << dep.get_name() << "(" << selector << ")";
  }

  enum query_type {
    FULL,
    POINT,
    NEGATIVE,
  } config = FULL;

  void set_negative()
  {
    if (is_negative())
      return;
    config = NEGATIVE;
    meta.negate_vars();
    meta.negative = true;
  }

  bool is_negative() const { return NEGATIVE == config; }
  void configure() override final
  {
    if (is_negative())
      return;
    if (!undo.count) {
      config = POINT;
    } else {
      config = FULL;
    }
  }

  using helper_t = eval_helper<Sel, Sel::has_value>;
  void eval_neg()
  {
    helper_t aux{selector};
    if (aux.find_in(origin))
      return;
    next(true);
  }

  void eval_point()
  {
    helper_t aux{selector};
    next(aux.find_in(origin));
  }

  void eval_full()
  {
    for (auto it = origin.iterator(); it; ++it)
      next(selector.unify(*it));
  }

  void eval() override final
  {
    switch (config) {
    case POINT: return eval_point();
    case FULL: return eval_full();
    case NEGATIVE: return eval_neg();
    }
  }
};

// FIXME: use the real machinery in json.h once ready
template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  return Json::Value();
}

template<typename Coll>
struct external_ : public Collection, iterable<typename remove_cvref<Coll>::type::value_type> {
  Coll coll;

  Json::Value to_json() const override final
  {
    return Json::Value() << id.get_name() << false << id.type_name();
  }

  Json::Value get_contents() const override final { return get_contents_common(coll /* TODO: columns */); }

  void print(std::ostream& os) const override final
  {
    os << id.get_name() << " = external<" << id.type_name() << ">, size=" << coll.size();
  }
  size_t merge(bool) override final { assert(false && "Cannot merge into external relations"); return 0; }

  using value_type = typename remove_cvref<Coll>::type::value_type;

  external_(const Coll& coll_, const ident& id)
    : Collection(id)
    , coll(coll_) {}

  stream<value_type> iterator() const override final { return generic_iterator(coll); }

  bool contains(const value_type& t) const override final { return do_contains(coll, t); }

  template<typename Sel>
  struct susp {
    external_& rel;
    Sel selector;

    struct Body : Matcher<Sel> {
      using Matcher<Sel>::Matcher;
    };

    operator Rule::ubody()
    {
      return Query::allocate<Body>(std::move(selector), rel, rel);
    }

    Rule::ubody operator!() {
      auto b = Query::allocate<Body>(std::move(selector), rel, rel);
      b->set_negative();
      return b;
    }
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return susp<decltype(detail::build_selector<value_type>(args...))>{*this, detail::build_selector<value_type>(std::forward<SelectArgs>(args)...)};
  }
};

template<typename T>
struct combine_ {
  void do_combine(T&, T&&, std::false_type) const {}
  void do_combine(T& out, T&& in, std::true_type) const
  {
    out.merge(std::move(in));
  }

  void operator()(T& out, T&& in) const
  {
    do_combine(out, std::move(in), std::is_base_of<datalol::lattice_tag_t, T>{});
  }
};

template<typename... Types>
struct combine_<std::tuple<Types...>> {
  using tuple_t = std::tuple<Types...>;
  void operator()(tuple_t& tout, tuple_t&& tin) const
  {
    for_each_in_tuple([](size_t, auto& out, auto&& in)
    {
      combine_<typename std::decay<decltype(in)>::type> comb{};
      comb(out, std::move(in));
      return true;
    }, tout, std::move(tin));
  }
};

template<typename T, typename Compare = std::less<void>>
struct table : public Collection {
  static_assert(!std::is_base_of<Var_, T>::value, "Cannot have var type!");
  using value_type = T;

  table(const char *name, const Compare& cmp = Compare{})
    : Collection(ident::make<table>(name))
    , stable(cmp)
    , recent(cmp)
  {}

  using relation_t = relation<T, Compare, combine_<T>>;
  relation_t stable;
  relation_t recent;
  std::vector<T> to_add;

  void insert(T&& t)
  {
    to_add.push_back(std::move(t));
  }

  size_t merge(bool recursive) override final
  {
    if (!recursive) {
      assert(stable.empty() && recent.empty());
      stable.assign(std::move(to_add));
      return 0;
    }
    // TODO: hold multiple `stable` relations, defer merges?
    // FIXME: indices
    stable.merge_from(std::move(recent));
    stable.erase_from(to_add);  // FIXME: erase_from should check lattice bound conditions
    recent.assign(std::move(to_add));
    assert(to_add.empty());
    return recent.size();
  }

  void print(std::ostream& os) const override final
  {
    os << this->id.get_name();
    if (recent.empty() && to_add.empty()) {
      print_(os, this->stable, "");
    } else {
      print_(os, this->stable, "stable: ");
      print_(os, this->recent, "recent: ");
      print_(os, this->to_add, "to_add: ");
    }
    // TODO: indices?
  }

  Json::Value to_json() const override final
  {
    return Json::Value() << id.get_name() << true << id.type_name();
  }

  Json::Value get_contents() const override final { return detail::get_contents_common(this->stable /* TODO: columns */); }

  template<typename S>
  void print_(std::ostream& os, const S& s, const char *title) const
  {
    auto name = this->id.get_name();
    os << title << " {";
    for (auto const& row : s)
      os << " " << name << "(" << detail::print_tuple<value_type>(row) << ")";
    os <<" }\n";
  }

  // This class is required, since when we create T(v1, v2, ...), we don't know wheather
  // its in head or body position
  template<typename Sel>
  struct susp {
    table& rel;
    Sel selector;

    struct Head : Rule::Head {
      Sel selector;
      table& rel;
      Head(Sel&& selector, table& rel)
        : Rule::Head(&rel)
        , selector(std::move(selector))
        , rel(rel)
      {
        selector.mark_vars(meta);
        meta.negate_vars();
      }
      void eval() override final
      {
        rel.insert(value_type(selector.get_value()));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << selector << ")";
      }
    };

    struct Body : detail::Matcher<Sel>, iterable<T> {
      table& tab;
      Body(Sel&& sel, table& rel)
        : Matcher<Sel>(std::move(sel), *this, rel)
        , tab(rel)
      {}

      stream<T> iterator() const override final
      {
        span<const relation_t> rels{nullptr, nullptr};
        if (this->is_negative())
          rels = {&tab.stable, 1};
        else
          switch (this->use_delta()) {
          case Rule::Body::RECENT: rels = {&tab.recent, 1}; break;
          case Rule::Body::STABLE: rels = {&tab.stable, 1}; break;
          case Rule::Body::BOTH:
          // contiguous members with same access specifier are contiguous in memory.
          // This returns both `stable` and `recent`.
            rels = {&tab.stable, 2}; break;
          }
                
        return stream<T>([rels]() mutable -> buf_t {
          while (!rels.empty()) {
            const relation_t *tab = rels.begin();
            ++rels;
            if (tab->data().size())
              return {tab->data().data(), tab->data().size()};
          }
          return {};
        });
      }
      bool contains(const T& t) const override final
      {
        if (this->is_negative())
          return tab.stable.contains(t);
        switch (this->use_delta()) {
        case Rule::Body::RECENT: return tab.recent.contains(t);
        case Rule::Body::STABLE: return tab.stable.contains(t);
        case Rule::Body::BOTH:
          return tab.stable.contains(t) || tab.recent.contains(t);
        }
        assert(false);
      }

      void print(std::ostream& os) const override final
      {
        if (!this->is_negative()) {
          switch (this->use_delta()) {
          case Rule::Body::RECENT: os << "δ"; break;
          case Rule::Body::STABLE: break;
          case Rule::Body::BOTH:   os << "δ⁺"; break;
          }
        }
        Matcher<Sel>::print(os);
      }
    };

    operator Rule::ubody()
    {
      return Query::allocate<Body>(std::move(selector), rel);
    }


    Rule::ubody operator!() {
      auto b = Query::allocate<Body>(std::move(selector), rel);
      b->set_negative();
      return b;
    }

    operator Rule::uhead()
    {
      return Query::allocate<Head>(std::move(selector), rel);
    }
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return susp<decltype(detail::build_selector<value_type>(args...))>{*this, detail::build_selector<value_type>(std::forward<SelectArgs>(args)...)};
  }
};

template<typename Fun, typename V>
struct binder_base : public Rule::Body {
  static_assert(std::is_base_of<thunk_tag_t, Fun>::value, "Must be a proper thunk");
  using bound_t = Var<typename std::decay<V>::type>;
  using thunk_t = Fun;
  thunk_t fun;
  bound_t var;
  binder_base(thunk_t&& fun, bound_t& var)
    : Rule::Body({{}, fun.captured_vars(), nullptr})
    , fun(std::move(fun))
    , var(std::move(var))
  {
    meta.produce.set(var.get_id());
    meta.produce &= ~meta.negative;     // In `i == $_(i->lol)`, we don't actually bind `i`
  }
};

template<typename Fun>
struct binder : public binder_base<Fun, typename Fun::result_t> {
  using binder_base<Fun, typename Fun::result_t>::binder_base;
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

template<typename Res>
struct iterate_ {
  using element_t = decltype(*std::begin(std::declval<Res>()));
  using binder_t = binder_base<thunk<Res>, element_t>;
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

template<typename T, bool = std::is_integral<T>::value>
struct step_iterator;

template<typename T>
struct step_iterator<T, false> {
  T i;
  ptrdiff_t step;
  bool operator!=(const step_iterator& o) { return i != o.i; }
  auto&& operator*() { return *i; }
  step_iterator& operator++() { i += step; return *this; }
};

template<typename T>
struct step_iterator<T, true> {
  T i;
  ptrdiff_t step;
  bool operator!=(step_iterator o) { return i != o.i; }
  auto operator*() { return i; }
  step_iterator& operator++() { i += step; return *this; }
};
static ptrdiff_t sign_of(ptrdiff_t d) { return (d > 0) - (d < 0); }

template<typename T>
struct xrange_ {
  T start, stop;
  ptrdiff_t step;
  using iterator = step_iterator<T>;
  xrange_(T start, T stop, ptrdiff_t step)
    : start(start), stop(stop), step(step)
  {}
  iterator begin() const { return iterator{start, step}; }
  iterator end() const { return iterator{stop, step}; }
};

template<typename Fun, typename std::enable_if<std::is_base_of<thunk_tag_t, Fun>::value>::type* = nullptr>
Rule::ubody operator==(Fun&& t, typename binder<Fun>::bound_t& var)
{
  return Query::allocate<binder<Fun>>(std::forward<Fun>(t), var);
}

}

template<typename Coll>
class external_ {
  detail::external_<Coll>& impl;
public:
  external_(detail::external_<Coll>& impl)
    : impl(impl)
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
};

template<typename Coll>
external_<Coll> external(Coll&& coll, const char *name)
{
  auto impl = Query::allocate<detail::external_<Coll>>(std::forward<Coll>(coll), ident::make<Coll>(name));
  return external_<Coll>(*impl);
}
template<typename Res>
auto iterate(thunk<Res>&& t)
{
  return detail::iterate_<Res>(std::move(t));
}

template<typename T>
auto xrange(T start, T stop, ptrdiff_t step = 0)
{
  ptrdiff_t len = stop - start;
  if (!step)
    step = detail::sign_of(len);
  auto rem = len % step;
  if (rem)
    stop += (step-rem);
  return detail::xrange_<T>(start, stop, step);
}

template<typename T>
std::enable_if<std::is_integral<T>::value, detail::xrange_<T>>
xrange(T stop)
{
  return xrange(0, stop, 1);
}

template<typename T, typename Compare = std::less<void>>
class table_base {
  detail::table<T>& impl;

public:
  table_base(const char *name)
    : impl(*Query::allocate<detail::table<T, Compare>>(name))
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
  auto externalize() && { return std::move(impl.stable); }
};

template<typename T0, typename... Rest>
struct table : public table_base<std::tuple<T0, Rest...>> {
  using table_base<std::tuple<T0, Rest...>>::table_base;
  static_assert(!std::is_base_of<Var_, T0>::value, "Cannot have var type!");
  static_assert(!detail::any<std::is_base_of<Var_, Rest>::value...>::value, "Cannot have var type");
};

}
