#include <cassert>
#include <algorithm>

#include <flat/pnr_utils.h>
#include <datalol/syntax.h>
#include <datalol/debug.h>

Query *Query::current = nullptr;
static Rule::vars_t *current_vars = nullptr;

Json::Value IPrint::to_json() const {
  std::ostringstream os;
  print(os);
  return Json::Value(os.str());
}

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

Rule::cursor::cursor(with_meta<Head>&& hh, with_meta<Body>&& bb)
{
  auto q = Query::current;

  r = q->start_rule();
  q->add_elem(hh.first, hh.second);
  append(std::move(bb));
}

void Rule::cursor::append(with_meta<Body>&& bb)
{
  Query::current->add_elem(bb.first, bb.second);
}

Rule::cursor::~cursor()
{
  Query::current->end_rule(r);
}

Rule::cursor operator<<(Rule::with_meta<Rule::Head>&& h, Rule::with_meta<Rule::Body>&& b)
{
  return Rule::cursor(std::move(h), std::move(b));
}

Rule::cursor& Rule::cursor::operator&(Rule::with_meta<Body>&& b)
{
  append(std::move(b));
  return *this;
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?" << impl.id.get_name() << "[" << impl.id.type_name() << "]";
  return os;
}

void Query::add_elem(const Rule::elem_meta& meta, const Rule::uelem& e)
{
  elems.push_back({meta, e});
}

Rule *Query::start_rule()
{
  Rule r;
  r.head = elems.size();
  rules.push_back(r);
  return &rules.back();
}

void Query::end_rule(Rule *r)
{
  assert(&rules.back() == r && !r->last);
  r->last = elems.size();
}

Query::Query(debug_info *dbg, const char *name)
  : pool(name)
  , dbg(dbg)
{
  current_query.set(&current, this);
}

void Query::iter::operator++()
{
  q->configure();
  q->run();
  q = nullptr;
}

void Query::print(std::ostream& os) const
{
  auto print_with_vars = [this, &os](const Rule::with_vars& vars, const Rule::Elem& e) {
    os << "[";
    print_vars(os, vars);
    os << "]";
    e.print(os);
  };
  os << "Query: {";
  size_t i=0;
  for (auto const& r : rules) {
    if (i) os << "\n";
    auto const& h = elems[r.head];
    print_with_vars(h.first.vars, *h.second);
    os << " << ";
    int count = 0;
    for (int j = r.head+1; j < r.last; j++) {
      os << (count++ ? " & " : "") << (recursive.test(j) ? "^" : "");
      auto const& b = elems[j];
      print_with_vars(b.first.vars, *b.second);
    }
    os << ";";
    i++;
  }
  os <<"}";
}

void Query::print_vars(std::ostream& os, const Rule::with_vars& vs) const
{
  int i=0;
  int out = 0;
  for (auto const& v : vars) {
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
  current_vars->set(v->impl->nvar);
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

thunk_base::thunk_base(const char *desc, const Rule::vars_t& vars)
  : desc(desc)
  , vars(vars)
{}
const Rule::vars_t& thunk_base::captured() const noexcept { return vars; }

std::ostream& operator<<(std::ostream& os, const thunk_base& t)
{
  return os << "THUNK(" << t.desc << ")";
}
