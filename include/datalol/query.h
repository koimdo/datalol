#pragma once

#include "syntax.h"
#include "relation.h"
#include "debug.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <type_traits>

namespace datalol {

namespace detail {
  struct unify_ {
    // Elementwise cases
    template<typename R> constexpr bool operator()(size_t, const R& s, const R& r) const { return s == r; }
    template<typename R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const { return s.unify(r); }
    template<typename S, typename R>
    constexpr bool operator()(size_t, const S& s, const R& r) const
    {
      static_assert(!std::is_base_of<Var_, S>::value, "Var type mismatch");
      return s == r;
    }
  };

  template<typename Sel, typename Row>
  bool unify(const Sel& sel, const Row& row)
  {
    return for_each_in_tuple(unify_{}, sel, row);
  }

  struct mark_vars_ {
    Rule::elem_meta& res;
    template<typename T> bool operator()(size_t, const T&) { return true; }
    template<typename T> bool operator()(size_t, const Var<T>& v)
    {
      res.produce.set(v.get_id());
      return true;
    }
  };

  template<typename Sel>
  void mark_vars(const Sel& sel, Rule::elem_meta& meta)
  {
    mark_vars_ mv{meta};
    for_each_in_tuple(mv, sel);
  }

  struct get_value {
    template<typename T>
    const T& operator()(const Var<T>& v) const { return *v.get(); }
    template<typename T>
    constexpr const T& operator()(const T& t) const { return t; }
  };

  struct generic_print {
    std::ostream& os;
    template<typename T>
    bool operator () (size_t i, T const &v)
    {
      os << (i? ", " : "") << v;
      return true;
    }
    template<typename T>
    bool operator () (size_t i, const Var<T>& v)
    {
      os << (i? ", " : "");
      v.print(os);
      return true;
    }
  };

  template<typename T>
  struct print_tuple {
    const T& t;
    print_tuple(const T& t): t(t) {}
    friend std::ostream& operator<<(std::ostream& os, const print_tuple& p)
    {
      os << "<";
      auto intr = !for_each_in_tuple(generic_print{os}, p.t);
      if (intr)
        os << ", ...";
      return os << ">";
    }
  };

// Backported from C++20
template<typename T>
struct remove_cvref {
  using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<typename T>
struct contain_trait {
  template<typename V>
  static bool contains(const T& coll, const V& val) { return coll.contains(val); }
};

template<typename T>
struct contain_trait<std::vector<T>> {
  template<typename V>
  static bool contains(const std::vector<T>& vec, const V& val)
  {
    return std::find(vec.begin(), vec.end(), val) != vec.end();
  }
};

template<typename Coll, typename V>
bool do_contains(const Coll& coll, const V& val)
{
  return contain_trait<typename remove_cvref<Coll>::type>::template contains(coll, val);
}

template<typename Derived, typename Sel, typename Origin>
struct Matcher_base : public Rule::Body {
  Sel selector;
  Origin& origin;

  using value_type = typename Origin::value_type;
  static_assert(std::tuple_size<Sel>::value == detail::tuple_lift<value_type>::size, "Inconsistent lengths");

  Matcher_base(Sel&& sel, Origin& origin)
    : Rule::Body(&origin)
    , selector(std::forward<Sel>(sel))
    , origin(origin)
  {
    mark_vars(sel, meta);
  }

  void print(std::ostream& os) const override final
  {
    os << origin.get_name() << "(" << print_tuple<Sel>(selector) << ")";
  }

  template<typename T>
  static
  const T& get_elem(const T& t) { return t; }
  static
  const value_type& get_elem(const std::tuple<value_type>& t) { return std::get<0>(t); }

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
  void config_impl()
  {
    if (is_negative())
      return;
    if (!undo.count) {
      config = POINT;
    } else {
      config = FULL;
    }
  }

  void eval_neg()
  {
    auto& self = static_cast<Derived&>(*this);
    auto t = transform_each(selector, get_value{});
    for (auto& coll : self.get_coll()) {
      if (do_contains(coll, get_elem(t))) {
        return;
      }
    }
    next(true);
  }

  void eval_point()
  {
    auto& self = static_cast<Derived&>(*this);
    auto t = transform_each(selector, get_value{});
    for (auto& coll : self.get_coll()) {
      next(do_contains(coll, get_elem(t)));
    }
  }

  void eval_full()
  {
    auto& self = static_cast<Derived&>(*this);
    for (auto& coll : self.get_coll()) {
      for (auto const& row : coll)
        next(unify(self.selector, row));
    }
  }

  void eval_impl()
  {
    switch (config) {
    case POINT: return eval_point();
    case FULL: return eval_full();
    case NEGATIVE: return eval_neg();
    }
  }
};

template<typename T>
struct sel_unwrap {
  using type = T;
  template<typename S>
  static
  auto unwrap(S&& s) { return std::forward<S>(s); }
};

template<typename T>
struct sel_unwrap<Var<T>&> {
  using type = Var<T>;
  static
  Var<T> unwrap(Var<T>& v) { return std::move(v); }
};

template<typename... Sel>
auto
build_selector(Sel&&... sel)
{
  return std::make_tuple(sel_unwrap<Sel>::unwrap(sel)...);
}

// FIXME: use the real machinery in json.h once ready
template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  return Json::Value();
}

template<typename Coll>
struct external_ : public Collection_base {
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
  size_t merge() override final { assert(false && "Cannot merge into external relations"); return 0; }

  using value_type = typename remove_cvref<Coll>::type::value_type;

  external_(const Coll& coll_, const ident& id)
    : Collection_base(id)
    , coll(coll_) {}

  template<typename Sel>
  struct susp {
    external_& rel;
    Sel selector;

    struct Body : Matcher_base<Body, Sel, external_> {
      using Matcher_base<Body, Sel, external_>::Matcher_base;

      span<typename std::remove_reference<Coll>::type>
      get_coll() const noexcept { return {&this->origin.coll, 1}; }

      void configure() override final { this->config_impl(); }
      void eval() override final { this->eval_impl(); }
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
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return susp<decltype(detail::build_selector(args...))>{*this, detail::build_selector(std::forward<SelectArgs>(args)...)};
  }
};

template<typename T, typename Compare = std::less<T>>
struct table : public Collection_base {
  static_assert(!std::is_base_of<Var_, T>::value, "Cannot have var type!");
  using value_type = T;

  table(const char *name, const Compare& cmp = Compare{})
    : Collection_base(ident::make<table>(name))
    , stable(cmp)
    , recent(cmp)
  {}

  relation<T, Compare> stable;
  relation<T, Compare> recent;
  std::vector<T> to_add;

  size_t merge() override final
  {
    // TODO: hold multiple `stable` relations, defer merges?
    // FIXME: indices
    stable.merge_from(std::move(recent));
    to_add.erase(std::remove_if(to_add.begin(), to_add.end(),
                                [this](const T& x) {
                                  return stable.contains(x);
                                }),
                 to_add.end());
    recent.assign(std::move(to_add));
    to_add.clear();
    return !recent.empty();
  }

  void print(std::ostream& os) const override final
  {
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
        mark_vars(selector, meta);
        meta.negate_vars();
      }
      void eval() override final
      {
        auto res = transform_each(selector, detail::get_value{});
        rel.to_add.push_back(std::move(res));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << detail::print_tuple<Sel>(selector) << ")";
      }
    };

    struct Body : detail::Matcher_base<Body, Sel, table> {
      using detail::Matcher_base<Body, Sel, table>::Matcher_base;
      span<relation<T, Compare>> get_coll() noexcept
      {
        if (this->is_negative())
          return {&this->origin.stable, 1};
        int delta = this->use_delta();
        if (delta < 0)
          return {&this->origin.stable, 1};
        else if (delta > 0)
          // contiguous members with same access specifier are contiguous in memory.
          // This returns both `stable` and `recent`.
          return {&this->origin.stable, 2};
        else // delta == 0
          return {&this->origin.recent, 1};
      }

      void configure() override final { this->config_impl(); }
      void eval() override final { this->eval_impl(); }
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
    return susp<decltype(detail::build_selector(args...))>{*this, detail::build_selector(std::forward<SelectArgs>(args)...)};
  }
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
  auto impl = Query::allocate<detail::external_<Coll>>(std::forward<Coll>(coll), ident::make<Coll>(name));
  return external_<Coll>(*impl);
}

template<typename...>
class table;

template<typename T>
class table<T> {
  detail::table<T>& impl;

public:
  table(const char *name)
    : impl(*Query::allocate<detail::table<T>>(name))
  {}

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) {
    return impl(std::forward<SelectArgs>(args)...);
  }
};

template<typename T0, typename T1, typename... Rest>
struct table<T0, T1, Rest...> : public table<std::tuple<T0, T1, Rest...>> {
  using table<std::tuple<T0, T1, Rest...>>::table;
  static_assert(!std::is_base_of<Var_, T0>::value, "Cannot have var type!");
  static_assert(!std::is_base_of<Var_, T1>::value, "Cannot have var type!");
  static_assert(!detail::any<std::is_base_of<Var_, Rest>::value...>::value, "Cannot have var type");
};

}
