// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
#include <bitset>
#include "flat/memory"
#include "flat/span"

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  friend std::ostream& operator<<(std::ostream& os, const IPrint& p) { return p.print(os), os; }
  virtual ~IPrint() {}
};

class Collection_base;
class Rule : public IPrint {
public:
  typedef std::bitset<64> vars_t;
  struct Head : IPrint {
    virtual void eval_head(Rule&) = 0;
    virtual Collection_base *collection() { return nullptr; }
  };
  struct Body : IPrint {
    virtual void eval_body(Rule&, size_t) = 0;
    virtual Collection_base *collection() { return nullptr; }
    Body *next = nullptr;
  };

  using uhead = flat::pool_ptr<Head>;
  using ubody = flat::pool_ptr<Body>;

  friend Rule& operator<<(uhead head, ubody b);
  friend Rule& operator& (Rule& rule, Rule::ubody e);
  friend class DQuery;

  uhead get_head() { return head; }
  flat::span<Body*> get_body() { return { reinterpret_cast<Body**>(body.data()), body.size() }; }
  size_t size() const { return body.size(); }

  size_t seminaive_current;     // FIXME: finer choice of Delta'd relation
  explicit Rule(uhead head);
private:
  void run(size_t current_delta);
  void append(ubody b);
  void print(std::ostream& os) const override;
  uhead head;
  std::vector<ubody> body;
  vars_t recursive;
};

Rule& operator<<(Rule::uhead head, Rule::ubody e);
Rule& operator& (Rule& rule, Rule::ubody e);

class cow_buf {
  static constexpr size_t MAX_SIZE = 1024;
  const void *p = nullptr;
  void (*destroy)(const void *) = nullptr;

  // Assumption: the held data is never aligned wider than std::align_t
  alignas(std::max_align_t) unsigned char buf[MAX_SIZE];

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
  void assign(T&& t)
  {
    // FIXME: wider alignment?
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
    std::string name;
    int id;
    mutable cow_buf p;
  };
  flat::pool_ptr<Impl> impl;
  friend class Query;

  friend
  std::ostream& operator<<(std::ostream& os, const Impl&);
public:
  Var_(const Var_&);
  Var_(const std::string& name = std::string());
  bool operator<(const Var_& o) const { return impl->name < o.impl->name; }
  void zap() const { impl->p.clear(); }
  bool is_unset() const { return !impl->p; }
  const std::string& get_name() const noexcept { return impl->name; }
};

class Query : public IPrint {
protected:
  std::vector<flat::pool_ptr<Rule>> rules;
  void print(std::ostream& os) const override;
  flat::autorelease pool;
  static Query *current;
  static Rule::vars_t *current_vars;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);
  friend class Var_;
  flat::pool_ptr<Var_::Impl> mkvar(const std::string& name);
  int get_id(flat::pool_ptr<Var_::Impl> core) const;
  std::vector<flat::pool_ptr<Var_::Impl>> vars;

  flat::guard with_query();
public:
  template<typename F>
  Query(F&& build)
    : pool("Query")
  {
    flat::autorelease::scoped guard(pool);
    flat::guard current_query = with_query();
    build();
  }
  Query(Query&&);
  static void print_vars(std::ostream& os, const Rule::vars_t& vs);
  static
  flat::guard with_vars(Rule::vars_t *dst);
};
