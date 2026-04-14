#include <cassert>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <limits>

#include <datalol/datalol>
#include <datalol/debug.h>

#define $_(expr, ...) THUNK(expr, ##__VA_ARGS__)

namespace datalol {

namespace detail {

struct compile_error : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

thread_local fluid_var<Query> Query::current;

std::string to_string(const IPrint& p) {
  std::ostringstream os;
  p.print(os);
  return os.str();
}

bool type_id_t::operator==(type_id_t o) const { return type == o.type; }
bool type_id_t::operator!=(type_id_t o) const { return type != o.type; }
bool type_id_t::operator<(type_id_t o) const { return type < o.type; }


type_id_t::type_id_t(const char *type)
{
  auto left = strchr(type, '=')+2;
  auto right = strrchr(left, ']');
  this->type = std::string{left, (size_t)(right-left)};
}

Collection::Collection(const ident& id)
  : id(id)
{
  Query::current->db.push_back(this);
}

Rule::elem_meta::elem_meta(const vars_t& produce, const vars_t& consume,
                           dependency *dep)
  : produce(produce), consume(consume)
  , dep(dep)
{
}
Rule::elem_meta::elem_meta(dependency *dep)
  : elem_meta({}, {}, dep)
{}

void Rule::elem_meta::negate_vars()
{
  // TODO: when adding negative scan, move those variables to `maybe consume`
  consume |= produce;
  produce.reset();
}

Rule& Rule::create_rule(Rule::uhead hh, Rule::ubody bb)
{
  auto q = Query::current.get();

  q->rules.push_back({});
  Rule& r = q->rules.back();
  r.head = hh.get();
  r.last = r.body = q->elems.size();
  r.append(bb);
  return r;
}

void Rule::append(ubody b)
{
  auto q = Query::current.get();

  assert(q->elems.size() == last);
  b->idx = last - body;
  q->elems.push_back(b.get());
  ++last;
}

Rule& operator<<(Rule::uhead&& h, Rule::ubody&& b)
{
  return Rule::create_rule(h, b);
}

Rule& Rule::operator&(Rule::ubody&& b)
{
  append(b);
  return *this;
}

void Var_::Impl::print_common(std::ostream& os) const
{
  auto const& name = id.get_name();
  if (name.empty())
    os << "?" << nvar << "[" << id.type_name() << "]";
  else
    os << "?" << name;
}

Query::Query(debug_info *dbg)
  : dbg(dbg)
{
}

void Query::print_rule(std::ostream& os, const Rule& r) const
{
  auto body = r.get_body(const_cast<Query&>(*this));
  os << *r.head;
  os << " << ";
  int j=0;
  for (auto b : body) {
    if (j) os << " & ";
    if (r.start == b) os << "@";
    if (r.recursive.test(j)) os << "^";
    os << *b;
    j++;
  }
}

void Query::print_stratum(std::ostream& os, const stratum& s) const
{
  os << "[";
  size_t i=0;
  for (auto const& r : s.extent) {
    if (i++) os << ";\n";
    print_rule(os, r);
  }
  os << "]";
  if (!s.to_merge.empty()) {
    os << " merge";
    for (auto d : s.to_merge)
      os << " " << *d;
  }
  os << "\n";
}

void Query::print(std::ostream& os) const
{
  os << "Query: {";
  size_t i=0;
  if (strata.empty()) {
    stratum all{{const_cast<Rule*>(rules.data()), rules.size()}, {}};
    print_stratum(os, all);
  } else {
    for (auto const& s : strata)
      print_stratum(os, s);
  }
  os <<"}";
}

vars_t& vars_t::operator|=(const vars_t& o) noexcept
{
  return vars |= o.vars, *this;
}
vars_t& vars_t::operator+=(const Var_& v) noexcept
{
  return vars.set(v.impl->nvar), *this;
}
vars_t& vars_t::operator-=(const vars_t& o) noexcept
{
  return vars &= ~o.vars, *this;
}
void vars_t::reset() noexcept
{
  vars.reset();
}
bool vars_t::test(const Var_& v) const noexcept
{
  return vars.test(v.impl->nvar);
}
bool vars_t::empty() const noexcept
{
  return vars.none();
}
size_t vars_t::count() const noexcept
{
  return vars.count();
}

Var_::Impl::Impl(ident id)
  : id(id)
{}

void Var_::register_var() const
{
  Query::current->current_vars += *this;
}

vars_t Var_::get_captured() noexcept
{
  vars_t& current_vars = Query::current->current_vars;
  auto res = current_vars;
  current_vars.reset();
  return res;
}

Rule::Elem::Elem(const elem_meta& m)
  : meta(m)
{
}

void Rule::Elem::configure()
{
}

thunk_base::thunk_base(std::string&& desc)
  : desc(std::move(desc))
  , vars(Var_::get_captured())
{
}

std::ostream& operator<<(std::ostream& os, const thunk_base& t)
{
  return os << "THUNK(" << t.desc << ")";
}

void Query::verify_neg(const Rule& r, const vars_t& bound, const Rule::Elem& e)
{
  auto neg = e.meta.consume;
  neg -= bound;
  if (!neg.empty()) {
    std::ostringstream error;
    error << "Unbound variables [";
    for (auto v : vars)
      if (neg.test(v))
        error << " " << v;
    error << " ] at element " << e << " in rule ";
    print_rule(error, r);
    throw compile_error(error.str());
  }
}

void Query::configure_rule(Rule& r, detail::span<int> order)
{
  // Step 3: set undo variables
  vars_t bound;
  std::vector<Var_> stack;
  auto body = r.get_body(*this);
  auto head = r.head;
  vars_t possible;
  for (auto b : body)
    possible |= b->meta.produce;
  stack.reserve(possible.count());
  for (auto ofs : order) {
    auto& elem = *body[ofs];
    auto pos = elem.meta.produce;
    verify_neg(r, bound, elem);

    pos -= bound;
    elem.undo.vars = stack.data() + stack.size();
    for (auto v : vars)
      if (pos.test(v))
        stack.push_back(v);
    elem.undo.count = (stack.data() + stack.size()) - elem.undo.vars;

    bound |= pos;
  }

  verify_neg(r, bound, *head);

  r.undo_stack = std::move(stack);

  // Chain rule body (and head) for execution
  Rule::Elem *next = head;
  for (auto it = order.end(), end = order.begin(); it != end; --it) {
    auto& e = *body[it[-1]];
    e.next_ = next;
    next = &e;
  }

  // Now that bound vars and order are known, configure elements for e.g. index access
  head->configure();
  for (auto b : body)
    b->configure();

  r.start = body[order[0]];
}

detail::span<Rule::Body*> Rule::get_body(Query& q) const noexcept
{
  auto base = q.elems.data();
  return {base+body, base+last};
}

void Query::configure()
{
  // Step 0: associate elements to rules (now that the don't move anymore)
  for (auto& r : rules)
    for (auto b : r.get_body(*this))
      b->rule_ = &r;

  // Step 1: stratify (if not manually-stratified before)
  if (strata.empty()) {
    stratify();
    for (auto& r : rules)
      for (auto b : r.get_body(*this))
        b->rule_ = &r;
  }

  // Step 3: set undo variables
  for (auto& r : rules) {
    std::vector<int> order;
    // TODO: smarter ordering
    for (int i=0; i<r.size(); i++)
      order.push_back(i);
    configure_rule(r, {order.data(), order.size()});
  }
  DEBUG_PROBE(BREAK_CONFIGURE);
}

struct dummy_dep : dependency {
  const Rule::Elem *e;
  dummy_dep(const Rule::Elem *e): e(e) {}
  void print(std::ostream& os) const override { e->print(os); }
  size_t merge(bool) override final { assert(false && "unreachable"); return 0; }
};
void Query::stratify()
{
  auto get_dep = [this](const Rule::Elem* e)
  {
    auto& meta = const_cast<Rule::elem_meta&>(e->meta);
    if (meta.dep)
      return meta.dep;
    auto d = pool.template allocate<dummy_dep>(e).get();
    return meta.dep = d;
  };
  auto times = DATALOL(stratifier) {
    auto Rules = external(rules, "rules");
    auto Elem = external(elems, "elems");

    using scc_t = lattice::lmax<int>;
    table<dependency*, dependency*, Rule::Body*> deps("deps"); // (body, head, e) if the rule containing `e` has head `h` and body `b`
    table<dependency*, dependency*> reach("reach"); // transitive closure of `deps`
    table<dependency*, dependency*> sameSCC("sameSCC");
    table<dependency*, scc_t> sccMap("sccMap");
    table<lattice::lmax<int>, dependency*> whenAll("whenAll");

    table<int, size_t, bool, const Rule*> when("when"); // time, minSCC representitive, rule
    Var<Rule*> rp("rulep");
    Var<Rule> rule("rule");
    Var<Rule::Body*> e("elem");
    Var<dependency*> body("body"), head("head"), d("d");
    Var<bool> is_recursive("is_recursive");
    LVar<lattice::lmax<int>> s("s"), t("t");
    Var<int> time("time");
    Var<int> scc("scc");
    LVar<scc_t> maxSCC("maxSCC");

    deps(body, head, e)  << Elem(e)
      & body == $_(get_dep(e))
      & head == $_(get_dep((*e)->rule_->head))
      ;

    reach(body, head) << deps(body, head, ignore);
    reach(body, head) << reach(body, d) & deps(d, head, ignore);

    // Possible enhancement for SCC once we get WCOJ:
    // https://www3.cs.stonybrook.edu/~warren/xsbbook/node18.html
    sameSCC(head, body) << reach(head, body) & reach(body, head);

    $_(throw compile_error("Cannot stratify negative cycle")) << deps(head, body, e) & sameSCC(head, body) & $_((*e)->meta.has_flags(Rule::FLAG_NEGATIVE));

    sccMap(head, scc) << (tie(scc, rule) == enumerate($_(rules))) & head == $_(get_dep(rule->head));
    sccMap(head, maxSCC) << sameSCC(head, body) & sccMap(body, maxSCC);

    // Topological sorting in datalog,
    // from https://lmeyerov.blogspot.com/2011/04/topological-sort-in-datalog.html
    whenAll(0, d) << Elem(e) & d == $_(get_dep(e)) & !deps(ignore, d, ignore);
    whenAll(t, head) << whenAll(t, body) &  sameSCC(head, body);
    whenAll(t, head) << whenAll(s, body) & reach(body, head) & !sameSCC(head, body) & t == s+1;

    $_((*e)->rule_->recursive.set(((*e)->idx))) << deps(body, head, e) & sameSCC(body, head);
    when(time, scc, is_recursive, rp) << whenAll(t, head) & time == $_(t.reveal())
      & deps(d, head, e)
      & sccMap(head, maxSCC)
      & scc == $_(maxSCC.reveal())
      & rp == $_((*e)->rule_)
      & is_recursive == $_((*rp)->recursive.any()); // Sort non-recursive before recursive

    // // TODO: mutable lambda directly in query?
    // $_(times.emplace_back(*t, *maxSCC, r.get(), *is_recursive), &times) << when(t, maxSCC, is_recursive, r);

    stratifier.manual_stratify({
        1, // deps

        2, // reach

        1, // sameSCC

        1, // negative stratification condition

        2, // sccMap

        3, // whenAll

        2, // set recursive, when
      });
    return when;
  };
  assert(times.size() == rules.size());

  // TODO: index-sort the rules in-place while adding strata.
  // See: https://stackoverflow.com/a/78397050
  std::vector<Rule> nrules;
  nrules.reserve(rules.size()); // Not an optimization - we want fixed rules location for add_stratum
  int time, last_time;
  size_t scc, last_scc;
  const Rule* rule;
  bool is_recursive;
  auto last = nrules.data();
  std::tie(last_time, last_scc, std::ignore, std::ignore) = *times.begin();
  for (auto const& ti : times) {
    std::tie(time, scc, is_recursive, rule) = ti;
    if (last_time != time || last_scc != scc) {
      auto nend = std::addressof(*nrules.end());
      add_stratum({last, nend});
      last = nend;

      last_time = time;
      last_scc = scc;
      // FIXME: collect multiple equally-timed nonrecursive rules into the same stratum?
    }
    nrules.push_back(std::move(*rule));
  }
  add_stratum({last, std::addressof(*nrules.end())});
  rules = std::move(nrules);
}

void Query::control::manual_stratify(std::initializer_list<unsigned> counts)
{
  assert(std::accumulate(counts.begin(), counts.end(), 0) == q->rules.size());
  Rule *last = q->rules.data();
  for (auto count : counts) {
    q->add_stratum({last, count});
    auto const& to_merge = q->strata.back().to_merge;
    for ( ; count; --count, ++last) {
      for (auto b : last->get_body(*q)) {
        auto dep = b->meta.dep;
        if (dep && std::find(to_merge.begin(), to_merge.end(), dep) != to_merge.end())
          last->recursive.set(b->idx);
      }
    }
  }
  assert(q->rules.data() + q->rules.size() == last);
}

void Query::add_stratum(detail::span<Rule> extent)
{
  std::vector<dependency*> to_merge;
  for (auto const& r : extent) {
    auto c = r.head->meta.dep;
    if (c && !dynamic_cast<dummy_dep*>(c))
      to_merge.push_back(c);
  }
  detail::relation<dependency*, std::less<>>::deduplicate(to_merge);
  strata.push_back(stratum{extent, std::move(to_merge)});
}

void Query::run_rule(Rule& r, int current_delta)
{
  current_rule = &r - rules.data();
  r.seminaive_current = current_delta;
  switch (policy) {
  case execution_policy::NESTED:
    r.start->eval();
    break;
  }
}

prov_t Query::get_provenance_internal() const
{
  int height = 0;
  for (auto const& v : vars) {
    if (!v.is_set()) continue;
    const void *p = v.get();
    for (auto* coll : db) {
      auto res = coll->find_needle(p);
      auto prov = res.second;
      if (res.first >= 0 && current_max_height > prov.height && prov.height > height)
        height = prov.height;
    }
  }
  return prov_t{current_rule, height + 1};
}

void Query::explain(const explain_t& rec, std::vector<explain_t>& next)
{
  assert(rec.prov.height > 0 && rec.prov.rule >= 0);
  auto& rule = rules[rec.prov.rule];
  auto it = rec.coll->get_nth(rec.idx);
  current_search = it.first;
  current_max_height = it.second.height;
  rule.head->configure();
  run_rule(rule, -1);
}

void Query::run()
{
  for (auto& srules : strata) {
    auto const& to_merge = srules.to_merge;

    // Nonrecursive rules come first
    auto beg = srules.extent.begin(), end = srules.extent.end();
    for ( ; beg != end ; ++beg) {
      Rule& r = *beg;
      if (r.recursive.none())
        run_rule(r, -1);
      else
        break;
    }
    if (beg == end) {
      // Non-recursive component: merge
      for (auto c : to_merge)
        c->merge(false);
      continue;
    }
    // Now, the recursive rules
    size_t changed = 1;
    for (int iter = 1; changed; iter++) {
      DEBUG_PROBE(BREAK_FIXPOINT);
      for (auto it = beg; it != end; ++it) {
        Rule& r = *it;
        assert(r.recursive.any()); // Only recursive rules!
        for (int i=0; i<r.size(); i++) {
          if (r.recursive.test(i))
            run_rule(r, i);
        }
      }
      changed = 0;
      for (auto c : to_merge)
        changed += c->merge(true);
    }
  }
  DEBUG_PROBE(BREAK_END);
  dbg->tripcount++;
}

} // namespace detail

} // namespace datalol
