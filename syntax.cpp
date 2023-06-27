#include <cassert>

#include "syntax.h"

// RULE ::= HEAD << body (& body)*
// (fact rules are not allowed)

// Query ::= Rule+
Rule::Rule(uhead head): head(std::move(head)) {}
Rule operator<<(Rule::uhead head, Rule::ubody b)
{
  Rule res(std::move(head));
  res.append(std::move(b));
  return res;
}

Rule operator& (Rule&& rule, Rule::ubody b) {
  Rule res(std::forward<Rule>(rule));
  res.append(std::move(b));
  return res;
}

void Rule::append(ubody b)
{
  body.emplace_back(std::move(b));
}
void Rule::print(std::ostream& os) const
{
  assert(head && !body.empty());
  os << *head << " << ";
  int i=0;
  for (auto const& b : body)
    os << (i++ ? " & " : "") << *b;
}

Query::Query(Rule&& r)
{
  rules.emplace_back(std::move(r));
}
Query Query::operator,(Rule&& r) &&
{
  rules.emplace_back(std::move(r));
  return std::move(*this);
}
Query operator,(Rule&& l, Rule&& r)
{
  return (Query(std::move(l)), std::move(r));
}

void Query::print(std::ostream& os) const
{
  os << "{";
  size_t i=0;
  for (auto const& r : rules)
    os << r << (rules.size() == ++i ? "" : ",\n");
  os <<"}";
}
