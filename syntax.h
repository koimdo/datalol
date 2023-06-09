// -*- C++ -*-
#pragma once

#include <vector>
#include <iostream>
#include <memory>

struct IPrint {
  virtual void print(std::ostream&) const = 0;
  friend std::ostream& operator<<(std::ostream& os, const IPrint& p) { return p.print(os), os; }
  virtual ~IPrint() {}
};

class Rule : public IPrint {
public:
  Rule(Rule&&) = default;
  struct Head : IPrint { virtual bool eval_head(Rule*) = 0; };
  struct Body : IPrint { virtual bool eval_body(Rule*) = 0; };

  using uhead = std::unique_ptr<Head>;
  using ubody = std::unique_ptr<Body>;

  uhead head;
  friend Rule operator<<(uhead head, ubody b);
  friend Rule operator& (Rule&& rule, Rule::ubody e);
  void append(ubody b);
private:
  void print(std::ostream& os) const override;
  std::vector<ubody> body;
  explicit Rule(uhead head);
};

Rule operator<<(Rule::uhead head, Rule::ubody e);
Rule operator& (Rule&& rule, Rule::ubody e);

class Query : public IPrint {
  std::vector<Rule> rules;
  void print(std::ostream& os) const override;
public:
  Query(Rule&&);
  Query operator,(Rule&& r) &&;
};

Query operator,(Rule&& l, Rule&& r);
