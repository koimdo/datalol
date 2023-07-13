// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
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
  struct Head : IPrint {
    virtual void eval_head(Rule&) = 0;
    virtual Collection_base *collection() { return nullptr; }
  };
  struct Body : IPrint {
    virtual void eval_body(Rule&, size_t) = 0;
    virtual Collection_base *collection() { return nullptr; }
  };

  using uhead = flat::pool_ptr<Head>;
  using ubody = flat::pool_ptr<Body>;

  friend Rule& operator<<(uhead head, ubody b);
  friend Rule& operator& (Rule& rule, Rule::ubody e);

  uhead get_head() { return head; }
  flat::span<Body*> get_body() { return { reinterpret_cast<Body**>(body.data()), body.size() }; }
  size_t size() const { return body.size(); }

  size_t seminaive_current;     // FIXME: finer choice of Delta'd relation
  explicit Rule(uhead head);
private:
  void append(ubody b);
  void print(std::ostream& os) const override;
  uhead head;
  std::vector<ubody> body;
};

Rule& operator<<(Rule::uhead head, Rule::ubody e);
Rule& operator& (Rule& rule, Rule::ubody e);

class Query : public IPrint {
protected:
  std::vector<flat::pool_ptr<Rule>> rules;
  void print(std::ostream& os) const override;
  flat::autorelease pool;
  static Query *current;
  friend Rule& operator<<(Rule::uhead head, Rule::ubody b);
public:
  template<typename F>
  Query(F&& build)
    : pool("Query")
  {
    flat::autorelease::scoped guard(pool);
    flat::guard current_query;
    current_query.set(&current, this);
    build();
  }
  Query(Query&&) = default;
};
