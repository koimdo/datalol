#include <cassert>
#include <algorithm>

#include <datalol/syntax.h>
#include <datalol/debug.h>

// RULE ::= HEAD << body (& body)*
// (fact rules are not allowed)

// Query ::= Rule+
Query *Query::current = nullptr;
static Rule::vars_t *current_vars = nullptr;

Rule::cursor::cursor(susp_Head&& h, susp_Body&& b)
{
  auto hh = h.apply_Head();
  auto bb = b.apply_Body();
  auto q = Query::current;

  r = q->start_rule();
  r->head = q->elems.size();
  q->add_elem(hh.first, hh.second);
  append(std::move(b));
}

void Rule::cursor::append(susp_Body&& b)
{
  auto bb = b.apply_Body();
  Query::current->add_elem(bb.first, bb.second);
}

Rule::cursor::~cursor()
{
  Query::current->end_rule(r);
}

Rule::cursor operator<<(Rule::susp_Head&& h, Rule::susp_Body&& b)
{
  return Rule::cursor(std::move(h), std::move(b));
}

Rule::cursor&& operator&(Rule::cursor&& r, Rule::susp_Body&& b)
{
  r.append(std::move(b));
  return std::move(r);
}

static std::ostream& print_with_vars(std::ostream& os, const Rule::with_vars& vars, const Rule::Elem& e)
{
  os << "[";
  Query::print_vars(os, vars);
  os << "]";
  e.print(os);
  return os;
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?";
  auto const& name = impl.name;
  if (name.empty())
    os << "<" << impl.id << ">";
  else
    os << name;
  os << "[" << impl.type << "]";
  return os;
}

void Query::add_elem(const Rule::elem_meta& meta, const Rule::uelem& e)
{
  elems.emplace_back(meta, e);
}

Rule *Query::start_rule()
{
  rules.emplace_back();
  return &rules.back();
}

void Query::end_rule(Rule *r)
{
  assert(&rules.back() == r);
  rules.back().last = elems.size();
}

Query::Builder::Builder(Query *q, debug_info *dbg, const char *)
  : q(q)
  , current_query(q->with_query())
{
  q->dbg = dbg;
}

void Query::Builder::iter::operator++()
{
  q->configure();
  q = nullptr;
}

Query::Query()
  : pool("Query")
  , dbg(nullptr)
{}

Query::Query(Query&&) = default;

void Query::print(std::ostream& os) const
{
  auto guard = const_cast<Query*>(this)->with_query();
  os << "Query: {";
  size_t i=0;
  for (auto const& r : rules) {
    if (i) os << "\n";
    auto const& h = elems[r.head];
    print_with_vars(os, h.first.vars, *h.second);
    os << " << ";
    int count = 0;
    for (int j = r.head+1; j < r.last; j++) {
      os << (count++ ? " & " : "") << (recursive.test(j) ? "^" : "");
      auto const& b = elems[j];
      print_with_vars(os, b.first.vars, *b.second);
    }
    os << ";";
    i++;
  }
  os <<"}";
}

void Query::print_vars(std::ostream& os, const Rule::with_vars& vs)
{
  int i=0;
  int out = 0;
  for (auto const& v : current->vars) {
    bool is_pos = vs.positive.test(i);
    bool is_neg = vs.negative.test(i);
    i++;
    if (!is_pos && !is_neg)
      continue;
    os << (out?", ":"") << (is_pos ? "+":"") << (is_neg ? "-":"") << v.get_name();
    out++;
  }
}

cow_buf::~cow_buf() { clear(); }
void cow_buf::clear()
{
  if (destroy)
    (destroy)(p);

  p = nullptr;
  destroy = nullptr;
}

Var_::Impl::Impl() = default;

flat::guard Rule::with_vars::capture_helper(Rule::vars_t *dst)
{
  flat::guard res;
  res.set(&current_vars, dst);
  return res;
}

void Var_::register_var(const Var_* v)
{
  assert(current_vars);
  current_vars->set(v->impl->id);
}

Rule::with_vars::with_vars(const Rule::vars_t& pos, nullptr_t) noexcept
  : positive(pos)
{}
Rule::with_vars::with_vars(nullptr_t, const Rule::vars_t& neg) noexcept
  : negative(neg)
{}
Rule::with_vars::with_vars(const Rule::vars_t& pos, const Rule::vars_t& neg) noexcept
  : positive(pos)
  , negative(neg)
{}


Rule::Elem::Elem(eval_t eval_)
  : eval_(eval_)
{
}

std::pair<flat::guard, flat::autorelease::scoped> Query::with_query()
{
  flat::guard res;
  res.set(&current, this);
  return std::make_pair(std::move(res), flat::autorelease::scoped(pool));
}
