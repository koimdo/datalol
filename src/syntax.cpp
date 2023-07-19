#include <cassert>
#include <algorithm>

#include <datalol/syntax.h>

// RULE ::= HEAD << body (& body)*
// (fact rules are not allowed)

// Query ::= Rule+
Query *current_query = nullptr;
Rule::vars_t *current_vars = nullptr;

Rule::Rule(uhead head): head(head)
{
  head->eval_ = head->eval_head;
  assert(head->negative.none() || head->positive.none());
  if (head->positive.any())
    std::swap(head->positive, head->negative);
}

Rule& operator<<(Rule::uhead head, Rule::ubody b)
{
  auto& rules = current_query->rules;
  rules.push_back(Rule(head));
  auto& res = rules.back();
  res.append(b);
  return res;
}

Rule& operator& (Rule& rule, Rule::ubody b) {
  rule.append(b);
  return rule;
}

void Rule::run(size_t current_delta)
{
  seminaive_current = current_delta;
  body[0]->eval(*this, 0);
}

void Rule::append(ubody b)
{
  assert(b->eval_);
  body.push_back(b);
}

static std::ostream& print_with_vars(std::ostream& os, const Rule::Elem& e)
{
  os << "[";
  Query::print_vars(os, e);
  return os << "]" << e;
}

void Rule::print(std::ostream& os) const
{
  assert(head && !body.empty());
  print_with_vars(os, *head) << " << ";
  int i=0;
  for (auto const& b : body) {
    print_with_vars(os << (i ? " & " : "") << (recursive.test(i) ? "^":""), *b);
    i++;
  }
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?";
  auto const& name = impl.name;
  if (name.empty())
    os << "<" << impl.id << ">";
  else
    os << name;
  return os;
}

Query::Query(Query&&) = default;
void Query::print(std::ostream& os) const
{
  flat::guard guard = const_cast<Query*>(this)->with_query();
  os << "{";
  size_t i=0;
  for (auto const& r : rules)
    os << r << (rules.size() == ++i ? "" : ",\n");
  os <<"}";
}

void Query::print_vars(std::ostream& os, const Rule::with_vars& vs)
{
  int i=0;
  int out = 0;
  for (auto v : current_query->vars) {
    bool is_pos = vs.positive.test(i);
    bool is_neg = vs.negative.test(i);
    i++;
    if (!is_pos && !is_neg)
      continue;
    os << (out?", ":"") << (is_pos ? "+":"") << (is_neg ? "-":"") << *v;
    out++;
  }
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
  : impl(current_query->mkvar(name).get(flat::unsafe_extract_pointer{}))
{
}


flat::guard Rule::with_vars::capture_helper(Rule::vars_t *dst)
{
  flat::guard res;
  res.set(&current_vars, dst);
  return res;
}

Var_::Var_(const Var_& v)
  : impl(v.impl)
{
  assert(current_vars);
  current_vars->set(impl->id);
}

Rule::with_vars::with_vars()
{
  if (current_vars)
    negative = *current_vars;
}
Rule::Elem::Elem(eval_t eval_)
  : eval_(eval_)
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

flat::guard Query::with_query()
{
  flat::guard res;
  res.set(&current_query, this);
  return res;
}
