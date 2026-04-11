#pragma once

#include "syntax.h"
#include "relation.h"
#include "selector.h"
#include "lattice.h"
#include "itertools.h"
#include "debug.h"
#include "json.h"

#include <cstddef>
#include <type_traits>
#include <utility>

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
    sel.mark_vars(meta.produce);
  }

  void print(std::ostream& os) const override
  {
    os << (is_negative() ? "!" : "") << dep.get_name() << "(" << selector << ")";
  }

  enum query_type {
    FULL,
    POINT,
    NEGATIVE,
    NEGASCAN,
  } config = FULL;

  void set_negative()
  {
    if (is_negative())
      return;
    meta.negate_vars();
    meta.set_flags(Rule::FLAG_NEGATIVE);
  }

  bool is_negative() const { return meta.has_flags(Rule::FLAG_NEGATIVE); }
  void configure() override final
  {
    if (is_negative()) {
      if (Sel::has_value)
        config = NEGATIVE;
      else
        config = NEGASCAN;
      return;
    }
    if (!undo.count && Sel::has_value) {
      config = POINT;
    } else {
      config = FULL;
    }
  }

  void eval_neg()
  {
    if (origin.contains(get_selector_value(selector)))
      return;
    next(true);
  }

  void eval_negascan()
  {
    for (auto it = origin.iterator(); it; ++it)
      if (unify(selector, *it))
        return;
    next(true);
  }

  void eval_point()
  {
    next(origin.contains(get_selector_value(selector)));
  }

  void eval_full()
  {
    for (auto it = origin.iterator(); it; ++it)
      next(unify(selector, *it));
  }

  void eval() override final
  {
    switch (config) {
    case POINT: return eval_point();
    case FULL: return eval_full();
    case NEGATIVE: return eval_neg();
    case NEGASCAN: return eval_negascan();
    }
  }
};

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
    do_combine(out, std::move(in), std::is_base_of<lattice::lattice_tag_base, T>{});
  }

  bool do_monus(T&, const T&, std::false_type) const { return false; }
  bool do_monus(T& out, const T& in, std::true_type) const
  {
    return out.monus(in);
  }

  bool monus(T& out, const T& in) const
  {
    return do_monus(out, in, std::is_base_of<lattice::lattice_tag_base, T>{});
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

  bool monus(tuple_t& tout, const tuple_t& tin) const
  {
    bool res = false;
    for_each_in_tuple([&res](size_t, auto& out, auto const& in)
    {
      combine_<typename std::decay<decltype(in)>::type> comb{};
      res |= comb.monus(out, in);
      return true;
    }, tout, tin);
    return res;
  }
};

template<typename T, typename Compare = std::less<void>>
struct table_ : public Collection {
  static_assert(!std::is_base_of<Var_, T>::value, "Cannot have var type!");
  using value_type = T;
  using prov_pair_t = std::pair<T, prov_t>;

  struct pair_compare {
    Compare base_cmp;
    pair_compare(const Compare& c = Compare{}) : base_cmp(c) {}
    bool operator()(const prov_pair_t& l, const prov_pair_t& r) const { return base_cmp(l.first, r.first); }
    bool operator()(const prov_pair_t& l, const T& r) const { return base_cmp(l.first, r); }
    bool operator()(const T& l, const prov_pair_t& r) const { return base_cmp(l, r.first); }
  };

  struct pair_combine {
    combine_<T> base_comb;
    pair_combine(const combine_<T>& c = combine_<T>{}) : base_comb(c) {}
    void operator()(prov_pair_t& out, prov_pair_t&& in) const
    {
      // Only combine if the T values are equal (which they should be if we're here)
      // Keep the earlier provenance (smaller iter or same)
      if (out.second.iter > in.second.iter)
        out.second = std::move(in.second);
      base_comb(out.first, std::move(in.first));
    }
    bool monus(prov_pair_t& out, const prov_pair_t& in) const
    {
      return base_comb.monus(out.first, in.first);
    }
  };

  table_(const ident& id, const Compare& cmp = Compare{})
    : Collection(id)
    , stable(cmp)
    , recent(cmp)
  {}

  using relation_t = relation<prov_pair_t, pair_compare, pair_combine>;
  relation_t stable;
  relation_t recent;
  std::vector<prov_pair_t> to_add;

  struct external_repr {
    relation_t rel;
    size_t size() const { return rel.size(); }
    span<const T> contents() const noexcept { return rel.contents()->*&prov_pair_t::first; }
    auto begin() const { return contents().begin(); }
    auto end() const { return contents().end(); }
  };

  external_repr externalize() && { return external_repr{std::move(stable)}; }

  void insert(T&& t)
  {
    to_add.push_back({std::move(t), Query::current->get_provenance()});
  }

  size_t merge(bool recursive) override final
  {
    if (!recursive) {
      assert(stable.empty() && recent.empty());
      stable.assign(std::move(to_add));
      return 0;
    }
    // TODO: hold multiple `stable` relations, defer merges?
    stable.merge_from(std::move(recent));
    // TODO: convert `to_add` into relation first, single-pass combined merge and monus with `stable` and `recent`?
    stable.erase_from(to_add);
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
      os << " " << name << "(" << detail::print_tuple<value_type>(row.first) << ")";
    os <<" }\n";
  }

  // This class is required, since when we create T(v1, v2, ...), we don't know wheather
  // its in head or body position
  template<typename Sel>
  struct susp {
    table_& rel;
    Sel selector;

    struct Head : Rule::Head {
      static_assert(Sel::has_value, "Cannot insert non-values");
      Sel selector;
      table_& rel;
      Head(Sel&& selector, table_& rel)
        : Rule::Head(&rel)
        , selector(std::move(selector))
        , rel(rel)
      {
        selector.mark_vars(meta.consume);
      }
      void eval() override final
      {
        rel.insert(value_type(get_selector_value(selector)));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << selector << ")";
      }
    };

    struct Body : detail::Matcher<Sel>, iterable<T> {
      table_& tab;
      Body(Sel&& sel, table_& rel)
        : Matcher<Sel>(std::move(sel), *this, rel)
        , tab(rel)
      {}

      stream<T> iterator() const override final
      {
        span<const relation_t> rels;
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
        return stream<T>([rels]() mutable -> detail::span<const T> {
          while (!rels.empty()) {
            detail::span<const T> res = rels.front().contents()->*&prov_pair_t::first;
            ++rels;
            if (!res.empty())
              return res;
          }
          return {};
        });
      }
      bool contains(const T& t) const override final
      {
        // For contains(T), check if any pair has a matching T value using the relation's contains with a pair
        if (this->is_negative())
          return tab.stable.contains(prov_pair_t{t, prov_t{}});
        switch (this->use_delta()) {
        case Rule::Body::RECENT: return tab.recent.contains(prov_pair_t{t, prov_t{}});
        case Rule::Body::STABLE: return tab.stable.contains(prov_pair_t{t, prov_t{}});
        case Rule::Body::BOTH:
          return tab.stable.contains(prov_pair_t{t, prov_t{}}) || tab.recent.contains(prov_pair_t{t, prov_t{}});
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
  auto impl = detail::Query::allocate<detail::external_<Coll>>(std::forward<Coll>(coll),
                                                               detail::ident::make<Coll>(name));
  return external_<Coll>(*impl);
}

template<typename Coll>
using external_t = external_<Coll>;

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
  detail::table_<T>& impl;

public:
  table_base(const detail::ident& id)
    : impl(*detail::Query::allocate<detail::table_<T, Compare>>(id))
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
  auto externalize() && { return std::move(impl).externalize(); }
};

template<typename T0, typename... Rest>
struct table : public table_base<std::tuple<T0, Rest...>> {
  using base_t = table_base<std::tuple<T0, Rest...>>;
  table(const char *name): base_t(detail::ident::make<table>(name)) {}
  static_assert(!std::is_base_of<detail::Var_, T0>::value, "Cannot have var type!");
  static_assert(!detail::any<std::is_base_of<detail::Var_, Rest>::value...>::value, "Cannot have var type");
};

}
