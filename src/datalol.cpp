#include <cassert>
#include <algorithm>
#include <cstring>
#include <limits>

#include <flat/pnr_utils.h>
#include <datalol/syntax.h>
#include <datalol/query.h>
#include <datalol/debug.h>

namespace datalol {
Query *Query::current = nullptr;
Var_::vars_t *Var_::current_vars = nullptr;

Json::Value IPrint::to_json() const {
  std::ostringstream os;
  print(os);
  return Json::Value(os.str());
}

bool type_id_t::operator==(type_id_t o) const { return !strcmp(type, o.type); }
bool type_id_t::operator!=(type_id_t o) const { return strcmp(type, o.type); }
bool type_id_t::operator<(type_id_t o) const { return strcmp(type, o.type) < 0; }


std::string type_id_t::type_name() const
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

Rule::cursor::cursor(uhead&& hh, ubody&& bb)
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

Rule::cursor operator<<(Rule::uhead&& h, Rule::ubody&& b)
{
  return Rule::cursor(std::move(h), std::move(b));
}

Rule::cursor& Rule::cursor::operator&(Rule::ubody&& b)
{
  Query::current->add_elem(b);
  return *this;
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?" << impl.id.get_name() << "[" << impl.id.type_name() << "]";
  return os;
}

void Query::add_elem(Rule::uelem me)
{
  me->idx = elems.size()-rules.back().head;
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
  std::cout << "\n";
  q->run();
  q = nullptr;
}

void Query::print_rule(std::ostream& os, const Rule& r) const
{
  auto print_with_vars = [this, &os](const Rule::Elem& e) {
    os << "[";
    print_vars(os, e.meta);
    os << "]";
    e.print(os);
  };
  auto const& h = elems[r.head];
  print_with_vars(*h);
  os << " << ";
  int count = 0;
  for (int j = r.head+1; j < r.last; j++) {
    os << (count++ ? " & " : "") << (r.seminaive_current == j-r.head ? "^" : "");
    auto const& b = elems[j];
    print_with_vars(*b);
  }
}

void Query::print(std::ostream& os) const
{
  os << "Query: {";
  size_t i=0;
  for (auto const& r : rules) {
    if (i) os << "\n";
    print_rule(os, r);
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

Var_::Impl::Impl(ident id)
  : id(id)
{}

void Var_::register_var(const Var_* v)
{
  assert(current_vars);
  current_vars->set(v->impl->nvar);
}

Rule::Elem::Elem(const elem_meta& m)
  : meta(m)
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

bool Query::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

static
void verify_neg(const Rule::vars_t& bound, const Rule& r, const Rule::elem_meta& e)
{
  auto neg = e.negative;
  neg &= ~bound;
  assert(neg.none());
}

Rule::elem_meta& Query::get_meta(unsigned i) { return elems[i]->meta; }
Rule::Elem& Query::get_elem(unsigned i) { return *elems[i]; }


void Query::configure_rule(Rule& r, flat::span<int> order)
{
  // Step 3: set undo variables
  Rule::vars_t bound;
  std::vector<Var_>& stack = r.undo_stack;
  stack.reserve(vars.size());
  for (auto ofs : order) {
    auto vars = get_meta(r.head+ofs);
    auto pos = vars.positive;
    verify_neg(bound, r, vars);

    pos &= ~bound;
    auto& elem = static_cast<Rule::Body&>(get_elem(r.head+ofs));
    elem.undo.vars = stack.data() + stack.size();
    for (auto v : this->vars)
      if (pos.test(v.get_id()))
        stack.push_back(v);
    elem.undo.count = (stack.data() + stack.size()) - elem.undo.vars;

    bound |= pos;
  }

  verify_neg(bound, r, get_meta(r.head));

  // Final step: chain rule body (and head) for execution
  auto next = &get_elem(r.head);
  next->rule_ = &r;
  for (auto it = order.rbegin(), end = order.rend(); it != end; ++it) {
    auto& e = static_cast<Rule::Body&>(get_elem(r.head+*it));
    e.next_ = next;
    next = &e;
    e.rule_ = &r;
  }
  r.start = r.head+order.front();

  // Step 4: configure leapers for undo stack
  // For var i, the possible leapers are those with i ∈ pos or neg ⊆ {var j : j < i}
  Rule::vars_t valid;
  for (auto v : r.undo_stack) {
    std::cerr << "At var " << *v.impl << " adding leapers [";
    std::vector<Rule::Body*> level;
    for (unsigned i=r.head+1; i<r.last; i++) {
      auto vars = get_meta(i);
      if (vars.positive.test(v.get_id()) || ((vars.negative & valid) == vars.negative)) {
        std::cerr << " " << get_elem(i);
        level.push_back(&static_cast<Rule::Body&>(get_elem(i)));
      } else {
        std::cerr << " !" << get_elem(i);
      }
    }
    std::cerr << " ]\n";
    r.leapers.push_back(std::move(level));
    valid.set(v.get_id());
  }
}

void Query::configure()
{
  // Step 1: figure out relations that are on the HEAD side
  for (auto& r : rules) {
    auto c = get_meta(r.head).collection;
    if (c)
      to_merge.insert(c);
  }
  // Step 2: Mark recursions in rule bodies
  // TODO: stratify using SCC on the rule graph.
  for (auto& r : rules) {
    for (size_t i=r.head+1; i<r.last; ++i) {
      auto coll = get_meta(i).collection;
      if (coll && to_merge.contains(coll)) {
        recursive.set(i);
        r.seminaive_current = i-r.head;
      }
    }
  }

  // Step 3: set undo variables
  for (auto& r : rules) {
    std::vector<int> order;
    // TODO: smarter ordering
    for (int i=r.head+1; i<r.last; i++)
      order.push_back(i-r.head);
    configure_rule(r, order);
  }
  DEBUG_PROBE(BREAK_CONFIGURE);
}

void Query::run_var(Rule& r, int vidx)
{
  if (vidx == r.undo_stack.size()) {
    // Everything determined
    auto& head = get_elem(r.head);
    std::cerr << "HEAD " << head << "\n";
    head.eval();
    return;
  }

  Var_ v = r.undo_stack[vidx];
  std::cerr << "Running var " << vidx << "/" << r.undo_stack.size() << " " << *v.impl << " leapers {";
  auto& leapers = r.leapers[vidx];
  size_t min_count = std::numeric_limits<size_t>::max();
  Rule::Body *min_leaper = nullptr;
  Rule::undo_pack undo{r.undo_stack.data()+vidx, r.undo_stack.size()-vidx};
  for (auto leaper : leapers) {
    auto count = leaper->count(undo);
    if (count < min_count) {
      min_count = count;
      min_leaper = leaper;
    }
    std::cerr << " " << *leaper << ": " << count;
  }
  std::cerr << " }\n";

  if (!min_count)
    return;

  min_leaper->propose(undo, v);
  for (auto l : leapers) {
    if (l != min_leaper)
      l->intersect(undo, v);
  }

  for (auto p : v.contents()) {
    v.set(p);
    run_var(r, vidx+1);
  }
  v.zap();
  v.clear_propose();
}

void Query::run_rule(Rule& r, size_t current_delta)
{
  r.seminaive_current = current_delta;
  std::cerr << "RUN rule ";
  Query::print_rule(std::cerr, r);
  std::cerr << " current_delta=" << current_delta << "\n";
  switch (policy) {
  case NESTED: return get_elem(r.start).eval();
  case WCOJ: return run_var(r, 0);
  }
}


void Query::explain(const std::string& coll, const void *target)
{

}

void Query::run()
{
  size_t changed = 0;
  for (auto& r : rules)
    if (!r.seminaive_current)
      // Run nonrecursive rules only once, before the recursive rules
      run_rule(r, 0);
  for (auto c : to_merge)
    changed += c->merge();
  for (int iter = 0; changed; iter++) {
    DEBUG_PROBE(BREAK_FIXPOINT);
    for (auto& r : rules) {
      if (!r.seminaive_current)
        continue;
      for (size_t i=r.head+1; i<r.last; ++i) {
        if (recursive.test(i))
          run_rule(r, i-r.head);
      }
    }
    changed = 0;
    for (auto c : to_merge)
      changed += c->merge();
  }
  DEBUG_PROBE(BREAK_END);
  dbg->tripcount++;
}

} // namespace datalol
