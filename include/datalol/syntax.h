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
  Rule(Rule&&) = default;
  struct Head : IPrint {
    virtual void eval_head(Rule&) = 0;
    virtual size_t post_head(Rule&) { return 0; };
    virtual Collection_base *collection() { return nullptr; }
  };
  struct Body : IPrint {
    virtual void eval_body(Rule&, size_t) = 0;
    virtual Collection_base *collection() { return nullptr; }
  };

  using uhead = flat::pool_ptr<Head>;
  using ubody = flat::pool_ptr<Body>;

  friend Rule operator<<(uhead head, ubody b);
  friend Rule operator& (Rule&& rule, Rule::ubody e);

  uhead get_head() { return head; }
  flat::span<Body*> get_body() { return { reinterpret_cast<Body**>(body.data()), body.size() }; }
  size_t size() const { return body.size(); }

  size_t seminaive_current;     // FIXME: finer choice of Delta'd relation
private:
  void append(ubody b);
  void print(std::ostream& os) const override;
  uhead head;
  std::vector<ubody> body;
  explicit Rule(uhead head);
};

Rule operator<<(Rule::uhead head, Rule::ubody e);
Rule operator& (Rule&& rule, Rule::ubody e);

class Query : public IPrint {
protected:
  std::vector<Rule> rules;
  void print(std::ostream& os) const override;
public:
  template<std::size_t N>
  Query(Rule(&&rs)[N])
  : rules(std::make_move_iterator(std::begin(rs)), std::make_move_iterator(std::end(rs)))
{}
  Query(Query&&) = default;
  Query(Rule&&);
  Query operator,(Rule&& r) &&;
};

Query operator,(Rule&& l, Rule&& r);
