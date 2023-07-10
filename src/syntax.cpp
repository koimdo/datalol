#include <cassert>

#include <datalol/syntax.h>

// RULE ::= HEAD << body (& body)*
// (fact rules are not allowed)

// Query ::= Rule+
Query *Query::current = nullptr;
Rule::Rule(uhead head): head(head) {}
Rule& operator<<(Rule::uhead head, Rule::ubody b)
{
  auto res = flat::allocate<Rule>(head);
  res->append(b);
  Query::current->rules.push_back(res);
  return *res;
}

Rule& operator& (Rule& rule, Rule::ubody b) {
  rule.append(b);
  return rule;
}

void Rule::append(ubody b)
{
  body.push_back(b);
}
void Rule::print(std::ostream& os) const
{
  assert(head && !body.empty());
  os << *head << " << ";
  int i=0;
  for (auto const& b : body)
    os << (i++ ? " & " : "") << *b;
}

void Query::print(std::ostream& os) const
{
  os << "{";
  size_t i=0;
  for (auto const& r : rules)
    os << *r << (rules.size() == ++i ? "" : ",\n");
  os <<"}";
}
