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

void Rule::run(size_t current_delta)
{
  seminaive_current = current_delta;
  body[0]->eval_body(*this, 0);
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
  for (auto const& b : body) {
    os << (i ? " & " : "") << (recursive.test(i) ? "^":"") << *b;
    i++;
  }
}

Query::Query(Query&&) = default;
void Query::print(std::ostream& os) const
{
  os << "{";
  size_t i=0;
  for (auto const& r : rules)
    os << *r << (rules.size() == ++i ? "" : ",\n");
  os <<"}";
}

cow_buf::~cow_buf() { clear(); }
void cow_buf::clear()
{
  if (!p)
    return;
  if (destroy)
    (destroy)(p);

  p = nullptr;
  destroy = nullptr;
}

Var_::Impl::Impl() = default;
Var_::Var_(const std::string& name)
  : impl(Query::current->mkvar(name))
{
}
flat::pool_ptr<Var_::Impl> Query::mkvar(const std::string& name)
{
  auto res = pool.template allocate<Var_::Impl>();
  res->name = name;
  res->id = vars.size();
  vars.push_back(res);
  return res;
}

