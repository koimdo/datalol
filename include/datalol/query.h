#pragma once

#include "syntax.h"
#include "debug.h"

#include <cstddef>
#include <datalol/tuple_util.h>
#include <array>

#include <flat/set>
#include <flat/map>
#include <type_traits>

namespace detail {
  struct unify_base {
    // Elementwise cases
    template<class R> constexpr bool operator()(size_t, const R& s, const R& r) const { return s == r; }
    template<class R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const
    {
      assert(false && "Must override");
      return false;
    }

    template<typename S, typename R>
    constexpr bool operator()(size_t, const S&, const R&) const
    {
      static_assert(std::is_same<S, R>::value, "Type mismatch");
      return false;             // Never reached
    }
  };
  struct unify_ : unify_base {
    template<class R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const { return s.unify(r); }
  };

  template<typename Sel, typename Row>
  bool unify(const Sel& sel, const Row& row)
  {
    return for_each_in_tuple(unify_{}, sel, row);
  }

  struct mark_vars_ {
    Rule::vars_t res;
    template<class T> bool operator()(size_t, const T&) { return true; }
    template<class T> bool operator()(size_t, const Var<T>& v)
    {
      res.set(v.get_id());
      return true;
    }
  };

  template<typename Sel>
  Rule::vars_t mark_vars(const Sel& sel)
  {
    mark_vars_ mv;
    for_each_in_tuple(mv, sel);
    return mv.res;
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
      Var<T>::do_print(os, v);
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

#define BASIC_TYPE(typ) \
  static Json::Value json_of(typ value) { return Json::Value(value); }
  BASIC_TYPE(Json::Int)
  BASIC_TYPE(Json::UInt)
  BASIC_TYPE(Json::Int64)
  BASIC_TYPE(Json::UInt64)
  BASIC_TYPE(double)
  BASIC_TYPE(const char *)
  BASIC_TYPE(const Json::String&)
  BASIC_TYPE(bool)
#undef BASIC_TYPE

  template<class T, typename = void>
  struct is_printable : std::false_type {};
  template<class T>
  struct is_printable<T, decltype(void(std::declval<std::ostream>() << std::declval<T>()))> : std::true_type {};

  template<class T>
  typename std::enable_if<is_printable<T>::value, Json::Value>::type
  json_of(const T& t) {
    std::ostringstream s;
    s << t;
    return Json::Value(s.str());
  }

  struct generic_json {
    Json::Value& vec;
    template<typename T>
    bool operator () (int i, T const &v)
    {
      vec.append(json_of(v));
      return true;
    }
  };

  template<typename... Args>
  Json::Value json_of(const std::tuple<Args...>& t)
  {
    Json::Value res;
    for_each_in_tuple(generic_json{res}, t);
    return res;
  };
}

template<typename Derived, typename Sel, typename Origin>
struct Matcher_base : public Rule::Body {
  Sel selector;
  Origin& origin;

  using value_type = typename Origin::value_type;
  static_assert(std::tuple_size<Sel>::value == detail::tuple_lift<value_type>::size, "Inconsistent lengths");

  Matcher_base(Sel&& sel, Origin& origin)
    : Rule::Body({detail::mark_vars(sel), {}, &origin})
    , selector(std::forward<Sel>(sel))
    , origin(origin)
  {}

  void print(std::ostream& os) const override final
  {
    os << origin.get_name() << "(" << detail::print_tuple<Sel>(selector) << ")";
  }

  size_t count_impl()
  {
    Derived& self = static_cast<Derived&>(*this);
    return self.get_coll().size();   // TODO: indexing, etc. for now it's a good hint
  }

  struct unify_propose : detail::unify_base {
    Var_ leapvar;
    unify_propose(Var_ leapvar): leapvar(leapvar) {}
    template<class R> constexpr bool operator()(size_t, const Var<R>& s, const R& r) const
    {
      if (!leapvar.is(s))
        return s.test(r);
      s.propose(r);
      return true;
    }
  };

  void propose_impl(Var_ out)
  {
    Derived& self = static_cast<Derived&>(*this);
    for (auto const& row : self.get_coll())
      for_each_in_tuple(unify_propose{out}, selector, row);
  }

  struct unify_intersect {
    Var_ leapvar;
    Derived& self;
    template<class T> constexpr bool operator()(size_t, const T& v) const { return true; }
    template<class T> constexpr bool operator()(size_t, const Var<T>& v) const
    {
      if (!leapvar.is(v))
        return true;
      v.retain_if([this]() {
        for (auto const& row : self.get_coll())
          if (detail::unify(self.selector, row))
            return true;
        return false;
      });
      return true;
    }
  };

  void intersect_impl(Var_ out)
  {
    Derived& self = static_cast<Derived&>(*this);
    for_each_in_tuple(unify_intersect{out, self}, selector);
  }

  void eval_impl()
  {
    Derived& self = static_cast<Derived&>(*this);
    for (auto const& row : self.get_coll()) {
      self.next(detail::unify(self.selector, row));
    }
  }
};

template<typename... Sel>
std::tuple<typename flat::remove_cvref<Sel>::type...>
build_selector(Sel&&... sel)
{
  return std::tuple<typename flat::remove_cvref<Sel>::type...>
    (std::forward<typename flat::remove_cvref<Sel>::type>(sel)...);
}

template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  Json::Value res;
  Json::Value& values = (res["values"] = Json::arrayValue);
  for (auto const& t : coll)
    values << detail::json_of(t);
  if (columns.size()) {
    Json::Value& jcolumns = (res["columns"] = Json::arrayValue);
    for (auto const& col : columns)
      jcolumns << col;
  }
  return res;
}

template<typename Coll>
class external_ : public Collection_base {
  Coll coll;
public:
  using value_type = typename flat::remove_cvref<Coll>::type::value_type;

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
  external_(const Coll& coll_, const ident& id)
    : Collection_base(id)
    , coll(coll_) {}

  template<typename... SelectArgs>
  Rule::ubody operator()(SelectArgs&&... args) {
    using selector_t = decltype(build_selector(std::forward<SelectArgs>(args)...));
    struct Body : Matcher_base<Body, selector_t, external_> {
      using Matcher_base<Body, selector_t, external_>::Matcher_base;
      const Coll& get_coll() const noexcept { return this->origin.coll; }

      size_t count() override final { return this->count_impl(); }
      void propose(Var_ out) override final { this->propose_impl(out); }
      void intersect(Var_ out) override final { this->intersect_impl(out); }
      void eval() override final { this->eval_impl(); }
    };

    return Query::allocate<Body>(build_selector(std::forward<SelectArgs>(args)...), *this);
  }
};

template<typename Coll>
external_<Coll> external(Coll&& coll, const char *name)
{
  return external_<Coll>(std::forward<Coll>(coll), ident::make<Coll>(name));
}

template<typename...>
struct table;

template<typename T>
struct table<T> : public Collection_base {
  table(const char *name)
    : Collection_base(ident::make<table>(name))
  {}

  static_assert(!std::is_base_of<Var_, T>::value, "Cannot have var type!");
  using value_type = T;

  flat::set<value_type> all;
  flat::set<value_type> delta, next_delta;

  void append(value_type&& t)
  {
    if (!all.contains(t))
      next_delta.insert(std::move(t));
  }

  size_t merge() override final
  {
    if (all.empty()) {
      std::swap(all, delta);
    } else if (!delta.empty()) {
      all = all.set_union(delta);
    }
    delta = next_delta.diff(all);
    // TODO: combined union/diff operation
    // FIXME: indices
    next_delta.clear();
    return delta.size();
  }

  void print(std::ostream& os) const override final
  {
    print_(os, this->all);
    // TODO: indices?
  }

  Json::Value to_json() const override final
  {
    return Json::Value() << id.get_name() << true << id.type_name();
  }

  Json::Value get_contents() const override final { return get_contents_common(this->all /* TODO: columns */); }

  template<class S>
  void print_(std::ostream& os, const S& s) const
  {
    auto name = this->id.get_name();
    os << "{";
    for (auto const& row : s)
      os << "\n  " << name << "(" << detail::print_tuple<value_type>(row) << ")";
    os <<"\n}";
  }

  template<typename... Args>
  void insert(Args&&... args) {
    value_type it(std::forward<Args>(args)...);
    this->all.insert(it);
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
        : Rule::Head({{}, detail::mark_vars(selector), &rel})
        , selector(std::move(selector))
        , rel(rel)
      {}
      void eval() override final
      {
        auto res = transform_each(selector, detail::get_value{});
        rel.append(std::move(res));
      }
      void print(std::ostream& os) const override final
      {
        os << rel.get_name() << "(" << detail::print_tuple<Sel>(selector) << ")";
      }
    };

    struct Body : Matcher_base<Body, Sel, table> {
      using Matcher_base<Body, Sel, table>::Matcher_base;
      const flat::set<value_type>& get_coll() noexcept
      {
        return this->use_delta() ? this->origin.delta : this->origin.all;
      }

      size_t count() override final { return this->count_impl(); }
      void propose(Var_ out) override final { this->propose_impl(out); }
      void intersect(Var_ out) override final { this->intersect_impl(out); }
      void eval() override final { this->eval_impl(); }
    };

    operator Rule::ubody()
    {
      return Query::allocate<Body>(std::move(selector), rel);
    }

    operator Rule::uhead()
    {
      return Query::allocate<Head>(std::move(selector), rel);
    }
  };

  template<typename... SelectArgs>
  auto operator()(SelectArgs&&... args) -> susp<decltype(build_selector(std::forward<SelectArgs>(args)...))> {
    return susp<decltype(build_selector(args...))>{*this, build_selector(std::forward<SelectArgs>(args)...)};
  }
};

template<typename T0, typename T1, typename... Rest>
struct table<T0, T1, Rest...> : public table<std::tuple<T0, T1, Rest...>> {
  using table<std::tuple<T0, T1, Rest...>>::table;
  static_assert(!std::is_base_of<Var_, T0>::value, "Cannot have var type!");
  static_assert(!std::is_base_of<Var_, T1>::value, "Cannot have var type!");
  static_assert(!detail::any<std::is_base_of<Var_, Rest>::value...>::value, "Cannot have var type");
};
