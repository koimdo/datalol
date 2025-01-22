#include <cassert>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <limits>

#include <datalol/syntax.h>
#include <datalol/query.h>
#include <datalol/lattice.h>
#include <datalol/debug.h>

#define $_(expr, ...) THUNK(expr, ##__VA_ARGS__)

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

Collection_base::Collection_base(const ident& id)
  : id(id)
{
  Query::current->db.push_back(this);
}

Rule::cursor::cursor(uhead&& hh, ubody&& bb)
{
  auto q = Query::current;

  r = q->start_rule();
  q->add_elem(hh.get());
  q->add_elem(bb.get());
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
  Query::current->add_elem(b.get());
  return *this;
}

std::ostream& operator<<(std::ostream& os, const Var_::Impl& impl)
{
  os << "?" << impl.id.get_name() << "[" << impl.id.type_name() << "]";
  return os;
}

void Query::add_elem(Rule::Elem *me)
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
  : name(name)
  , dbg(dbg)
  , old_current(current)
{
  current = this;
}

Query::~Query()
{
  current = old_current;
}

void Query::iter::operator++()
{
  q->configure();
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
    bool is_pos = vs.produce.test(i);
    bool is_neg = vs.consume.test(i);
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

void Rule::Elem::configure()
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

static
void verify_neg(const Rule::vars_t& bound, const Rule& r, const Rule::elem_meta& e)
{
  auto neg = e.consume;
  neg &= ~bound;
  assert(neg.none());
}

Rule::elem_meta& Query::get_meta(unsigned i) { return elems[i]->meta; }
Rule::Elem& Query::get_elem(unsigned i) { return *elems[i]; }


void Query::configure_rule(Rule& r, detail::span<int> order)
{
  // Step 3: set undo variables
  Rule::vars_t bound;
  std::vector<Var_>& stack = r.undo_stack;
  stack.reserve(vars.size());
  for (auto ofs : order) {
    auto vars = get_meta(r.head+ofs);
    auto pos = vars.produce;
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

  // Chain rule body (and head) for execution
  auto next = &get_elem(r.head);
  for (auto it = order.end(), end = order.begin(); it != end; --it) {
    auto& e = static_cast<Rule::Body&>(get_elem(r.head+it[-1]));
    e.next_ = next;
    next = &e;
  }

  // Now that bound vars and order are known, configure elements for e.g. index access
  for (unsigned i=r.head; i != r.last; i++)
    get_elem(i).configure();

  r.start = r.head+order[0];
}

void Query::configure()
{
  // Step 0: associate elements to rules (now that the don't move anymore)
  for (auto& r : rules)
    for (auto i=r.head; i<r.last; i++)
      get_elem(i).rule_ = &r;

  // Step 1: stratify (if not manually-stratified before)
  if (strata.empty())
    stratify();

  // Step 2: Mark recursions in rule bodies
  for (auto& srules : strata) {
    for (auto& r : srules.extent) {
      for (size_t i=r.head+1; i<r.last; ++i) {
        auto coll = get_meta(i).collection;
        auto& to_merge = srules.to_merge;
        if (coll && std::find(to_merge.begin(), to_merge.end(), coll) != to_merge.end()) {
          recursive.set(i);
          r.seminaive_current = i-r.head;
        }
      }
    }
  }

  // Step 3: set undo variables
  for (auto& r : rules) {
    std::vector<int> order;
    // TODO: smarter ordering
    for (int i=r.head+1; i<r.last; i++)
      order.push_back(i-r.head);
    configure_rule(r, {order.data(), order.size()});
  }
  DEBUG_PROBE(BREAK_CONFIGURE);
}

void Query::stratify()
{
  std::vector<Rule*> rules_(rules.size());
  std::iota(rules_.begin(), rules_.end(), rules.data());

  struct sccmap_ {
    std::vector<min_lattice<size_t>> m;
    const Rule *base;
    sccmap_(const std::vector<Rule>& rules)
      : m(rules.size())
      , base(rules.data())
    {}
    size_t ofs(const Rule *r) const { return r-base; }
    void set(const Rule *l, const Rule *r) { m[ofs(l)] |= ofs(r); }
    size_t get(const Rule *r) const { return m[ofs(r)].get(); }
    bool same(const Rule *l, const Rule *r) const { return m[ofs(l)] == m[ofs(r)]; }
  } sccMap(rules);

  std::vector<std::tuple<int, size_t, Rule*>> times;
  DATALOL(stratifier) {
    auto R = external(rules_, "rules"); // FIXME: allow `external()` on vectors
    auto Elem = external(elems, "elems");

    table<Rule*, Rule*, bool> deps("deps"), reach("reach"); // (rh, rb, N) if head of `rh` is in body of `rb` and body in `rb` is negative
    table<int, Rule*> whenAll("whenAll");
    table<int, size_t, Rule*> when("when"); // time, minSCC representitive, rule
    Var<Rule*> rh, rb, rc;
    Var<Rule::Elem*> e;
    Var<Collection_base*> body;
    Var<bool> neg, nl, nr;
    Var<int> s, t;
    Var<size_t> minSCC;

    deps(rh, rb, neg) << Elem(e) & $_(dynamic_cast<Rule::Body*>(*e)) & rb == $_((*e)->rule_)
      & body == $_((*e)->meta.collection) & $_(*body)
      & R(rh) & body == $_(this->get_elem((*rh)->head).meta.collection)
      & neg == $_((*e)->meta.negative);


    // TODO: when we get lattice-valued relations, `reach` is lmap({rh, rb} -> lbool)
    reach(rh, rb, neg) << deps(rh, rb, neg);
    reach(rh, rb, neg) << reach(rh, rc, nl) & deps(rc, rb, nr) & neg == $_(*nl || *nr);

    $_(throw std::logic_error("Cannot stratify")) << reach(rb, rb, true);

    // Possible enhancement for SCC once we get WCOJ:
    // https://www3.cs.stonybrook.edu/~warren/xsbbook/node18.html

    // Kludge due to current lack of min-aggergation
    $_(sccMap.set(*rb, *rb), &sccMap) << R(rb);
    $_(sccMap.set(*rb, *rh), &sccMap) << reach(rh, rb, nl) & reach(rb, rh, nr);

    // Topological sorting in datalog,
    // from https://lmeyerov.blogspot.com/2011/04/topological-sort-in-datalog.html

    whenAll(0, rb) << R(rb) & R(rh) & !reach(rh, rb, false) & !reach(rh, rb, true);

    whenAll(s, rb) << whenAll(s, rh) & R(rb)              & $_( sccMap.same(*rh, *rb), &sccMap);
    whenAll(t, rb) << whenAll(s, rh) & reach(rh, rb, neg) & $_(!sccMap.same(*rh, *rb), &sccMap) & t == $_(*s+1);

    when(s, minSCC, rb) << whenAll(s, rb) & t == $_(*s+1) & !whenAll(t, rb)
      & minSCC == $_(sccMap.get(*rb), &sccMap); // figure out binder typing on value vs. reference

    $_(times.push_back({*t, *minSCC, *rb}), &times) << when(t, minSCC, rb);

    // At the end: index-sort the rules while adding strata
    // See: https://stackoverflow.com/a/78397050
    stratifier.manual_stratify({
        1, // deps
        2, // reach

        3, // negative stratification condition, sccMap

        3, // whenAll(0, r) // FIXME: why 1, 2 fails here?

        1, // when
        1, // times
      });
  }
  assert(times.size() == rules.size());
  // FIXME: really stratify
  unsigned all = rules.size();
  return do_stratify({&all, 1});
}

void Query::do_stratify(detail::span<const unsigned> counts)
{
  Rule *last = rules.data();
  for (auto count : counts) {
    detail::span<Rule> extent{last, count};
    last += count;
    std::vector<Collection_base*> to_merge;
    for (auto const& r : extent) {
      auto c = get_meta(r.head).collection;
      if (c)
        to_merge.push_back(c);
    }
    std::sort(to_merge.begin(), to_merge.end());
    to_merge.erase(std::unique(to_merge.begin(), to_merge.end()), to_merge.end());
    strata.push_back(stratum{extent, std::move(to_merge)});
  }
  assert(rules.data() + rules.size() == last);
}

void Query::run_rule(Rule& r, size_t current_delta)
{
  r.seminaive_current = current_delta;
  switch (policy) {
  case NESTED: return get_elem(r.start).eval();
  }
}


void Query::explain(const std::string& coll, const void *target)
{

}

void Query::run()
{
  for (auto& srules : strata) {
    auto const& to_merge = srules.to_merge;

    size_t changed = 1;
    for (int iter = 0; changed; iter++) {
      DEBUG_PROBE(BREAK_FIXPOINT);
      for (auto& r : srules.extent) {
        if (!r.seminaive_current)
          run_rule(r, 0);
        for (size_t i=r.head+1; i<r.last; ++i) {
          if (recursive.test(i))
            run_rule(r, i-r.head);
        }
      }
      changed = 0;
      for (auto c : to_merge)
        changed += c->merge();
    }
  }
  DEBUG_PROBE(BREAK_END);
  dbg->tripcount++;
}

} // namespace datalol
