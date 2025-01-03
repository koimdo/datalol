#include <cassert>
#include <algorithm>

#include <flat/pnr_utils.h>
#include <datalol/syntax.h>
#include <datalol/debug.h>

Query *Query::current = nullptr;
Var_::vars_t *Var_::current_vars = nullptr;

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
  return std::string();
}

Rule::cursor::cursor(with_meta<Head>&& hh, with_meta<Body>&& bb)
{
  auto q = Query::current;

  r = q->start_rule();
  q->add_elem(hh);
  q->add_elem(bb);
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
  Query::current->add_elem(b);
  return *this;
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?" << impl.id.get_name() << "[" << impl.id.type_name() << "]";
  return os;
}

void Query::add_elem(const Rule::with_meta<Rule::Elem>& me)
{
  elems.push_back(me);
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
  q->print(std::cout);          // TODO: remove printf
  q->run();
  q = nullptr;
}

void Query::print(std::ostream& os) const
{
  auto print_with_vars = [this, &os](const Rule::elem_meta& vars, const Rule::Elem& e) {
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
    print_with_vars(h.first, *h.second);
    os << " << ";
    int count = 0;
    for (int j = r.head+1; j < r.last; j++) {
      os << (count++ ? " & " : "") << (recursive.test(j) ? "^" : "");
      auto const& b = elems[j];
      print_with_vars(b.first, *b.second);
    }
    os << ";";
    i++;
  }
  os <<"}";
}

void Query::print_vars(std::ostream& os, const Rule::elem_meta& vs) const
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

Var_::Impl::~Impl() { clear(); }
void Var_::Impl::clear()
{
  if (destroy)
    (destroy)(p);

  p = nullptr;
  destroy = nullptr;
}

Var_::Impl::Impl() = default;

void Var_::register_var(const Var_* v)
{
  assert(current_vars);
  current_vars->set(v->impl->nvar);
}

Rule::Elem::Elem(eval_t eval_)
  : eval_(eval_)
{
}

thunk_base::thunk_base(const char *desc)
  : desc(desc)
  , vars(*Var_::current_vars)
{
  Var_::current_vars = nullptr;
}

std::ostream& operator<<(std::ostream& os, const thunk_base& t)
{
  return os << "THUNK(" << t.desc << ")";
}
