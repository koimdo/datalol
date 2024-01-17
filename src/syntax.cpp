#include <cassert>
#include <algorithm>

#include <flat/pnr_utils.h>
#include <datalol/syntax.h>
#include <datalol/debug.h>

Query *Query::current = nullptr;
Builder *Builder::current = nullptr;
static Rule::vars_t *current_vars = nullptr;

std::string ident::type_name() const noexcept
{
  auto left = strchr(type, '=')+2;
  auto right = strrchr(left, ']');
  return std::string(left, right-left);
}

std::string ident::get_name() const
{
  if (name)
    return name;
  return format{} << "<" << id << ">";
}

Rule::cursor::cursor(susp_Head&& h, susp_Body&& b)
{
  auto hh = h.apply_Head();
  auto bb = b.apply_Body();
  auto q = Builder::current;

  r = q->start_rule();
  q->add_elem(hh.first, hh.second);
  append(std::move(b));
}

void Rule::cursor::append(susp_Body&& b)
{
  auto bb = b.apply_Body();
  Builder::current->add_elem(bb.first, bb.second);
}

Rule::cursor::~cursor()
{
  Builder::current->end_rule(r);
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
  os << "?" << impl.id.get_name() << "[" << impl.id.type << "]";
  return os;
}

void Builder::add_elem(const Rule::elem_meta& meta, const Rule::uelem& e)
{
  q->elems.emplace_back(meta, e);
}

Rule *Builder::start_rule()
{
  Rule r;
  r.head = q->elems.size();
  q->rules.push_back(r);
  return &q->rules.back();
}

void Builder::end_rule(Rule *r)
{
  assert(&q->rules.back() == r && !r->last);
  q->rules.back().last = q->elems.size();
}

Builder::Builder(Query *q, debug_info *dbg, const char *)
  : q(q)
  , current_query(q->with_query())
{
  q->dbg = dbg;
  current_builder.set(&current, this);
}

void Builder::iter::operator++()
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
    os << (out?", ":"") << (is_pos ? "+":"") << (is_neg ? "-":"") << v.impl->id.get_name();
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
  current_vars->set(v->impl->id.id);
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

thunk_base::thunk_base(const char *desc, const Rule::vars_t& vars)
  : desc(desc)
  , vars(vars)
{}
const Rule::vars_t& thunk_base::captured() const noexcept { return vars; }

std::ostream& operator<<(std::ostream& os, const thunk_base& t)
{
  return os << "THUNK(" << t.desc << ")";
}
