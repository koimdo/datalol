#include <cassert>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <limits>

#include <datalol/syntax.h>
#include <datalol/query.h>
#include <datalol/debug.h>

#define $_(expr, ...) THUNK(expr, ##__VA_ARGS__)

namespace datalol {

struct compile_error : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

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

Collection::Collection(const ident& id)
  : id(id)
{
  Query::current->db.push_back(this);
}

Rule::elem_meta::elem_meta(dependency *dep)
  : dep(dep)
  , negative(false)
{}

void Rule::elem_meta::negate_vars()
{
  // TODO: when adding negative scan, move those variables to `maybe consume`
  consume |= produce;
  produce.reset();
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

void Var_::Impl::print_common(std::ostream& os) const
{
  auto name = id.get_name();
  if (name.empty())
    os << "?" << nvar << "[" << id.type_name() << "]";
  else
    os << "?" << name;
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

void Var_::register_var(const Var_::Impl* v)
{
  current_vars->set(v->nvar);
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

void Query::verify_neg(const Rule::vars_t& bound, const Rule::Elem& e)
{
  auto neg = e.meta.consume;
  neg &= ~bound;
  if (!neg.none()) {
    std::ostringstream error;
    error << "Unbound variables [";
    for (auto v : vars)
      if (neg.test(v.get_id()))
        error << " " << v;
    error << " ] at element " << e << " in rule ";
    print_rule(error, *e.rule_);
    throw compile_error(error.str());
  }
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
    auto& elem = static_cast<Rule::Body&>(get_elem(r.head+ofs));
    auto pos = elem.meta.produce;
    verify_neg(bound, elem);

    pos &= ~bound;
    elem.undo.vars = stack.data() + stack.size();
    for (auto v : vars)
      if (pos.test(v.get_id()))
        stack.push_back(v);
    elem.undo.count = (stack.data() + stack.size()) - elem.undo.vars;

    bound |= pos;
  }

  verify_neg(bound, get_elem(r.head));

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
  if (strata.empty()) {
    stratify();
    for (auto& r : rules)
      for (auto i=r.head; i<r.last; i++)
        get_elem(i).rule_ = &r;
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

template<typename T>
struct sccmap {
  std::map<T, size_t> m; // t -> (id, representitive)
  size_t& get(const T& t)
  {
    size_t n = m.size();
    auto itb = m.insert({t, n});
    return itb.first->second;
  }
  void unify(const T& l, const T& r)
  {
    get(l) = std::max(get(l), get(r));
  }
  bool same(const T& l, const T& r)
  {
    auto sl = get(l), sr = get(r);
    return sl == sr;
  }
};

template<typename D>
class depwrap : public dependency {
  D* d;
  void print(std::ostream& os) const override final { os << "wrap<" << ident::make<D>().type_name() << ">"; }
  size_t merge(bool) override final { return 0; }
public:
  depwrap(D& d): d(&d) {}
  D* operator->() const { return d; }
};

struct dummy_dep : dependency {
  const Rule::Elem *e;
  dummy_dep(const Rule::Elem *e): e(e) {}
  void print(std::ostream& os) const override { e->print(os); }
  size_t merge(bool) override final { assert(false && "unreachable"); }
};
void Query::stratify()
{
  sccmap<dependency*> sccMap_;
  depwrap<decltype(sccMap_)> sccMap(sccMap_);

  std::vector<std::tuple<int, size_t, const Rule*, bool>> times;
  auto get_dep = [this](const Rule::Elem* e)
  {
    auto& meta = const_cast<Rule::elem_meta&>(e->meta);
    if (meta.dep)
      return meta.dep;
    auto d = pool.template allocate<dummy_dep>(e).get();
    return meta.dep = d;
  };
  DATALOL(stratifier) {
    auto Rules = external(rules, "rules");
    auto Elem = external(elems, "elems");

    table<dependency*, dependency*, Rule::Elem*> deps("deps"); // (body, head, e) if the rule containing `e` has head `h` and body `b`
    table<dependency*, dependency*> reach("reach"); // transitive closure of `deps`
    table<int, dependency*> whenAll("whenAll");

    table<int, size_t, bool, reference<const Rule>> when("when"); // time, minSCC representitive, rule
    Var<Rule> r("rule");
    Var<Rule::Elem*> e("elem");
    Var<dependency*> body("body"), head("head"), d("d");
    Var<bool> is_recursive("is_recursive");
    Var<int> s("s"), t("t");
    Var<size_t> maxSCC("maxSCC");

    deps(body, head, e)  << Elem(e) & $_(dynamic_cast<Rule::Body*>(*e))
      & body == $_(get_dep(*e))
      & head == $_(get_dep(&this->get_elem((*e)->rule_->head)))
      ;

    reach(body, head) << deps(body, head, ignore);
    reach(body, head) << reach(body, d) & deps(d, head, ignore);

    // Kludge due to current lack of max-semilattice aggregation
    // Possible enhancement for SCC once we get WCOJ:
    // https://www3.cs.stonybrook.edu/~warren/xsbbook/node18.html
    $_(sccMap->unify(*head, *body)) << reach(head, body) & reach(body, head);

    $_(throw compile_error("Cannot stratify negative cycle")) << deps(head, body, e) & $_(sccMap->same(*head, *body)) & $_((*e)->meta.negative);

    // Topological sorting in datalog,
    // from https://lmeyerov.blogspot.com/2011/04/topological-sort-in-datalog.html
    whenAll(0, d) << Elem(e) & d == $_(get_dep(*e)) & !deps(ignore, d, ignore);
    whenAll(t, head) << whenAll(s, body) & reach(body, head) & t == $_(*s + !sccMap->same(*head, *body));

    $_((*e)->rule_->recursive.set(((*e)->idx))) << deps(body, head, e) & $_(sccMap->same(*body, *head));
    when(s, maxSCC, is_recursive, r) << whenAll(s, head) & t == $_(*s+1) & !whenAll(t, head)
      & deps(d, head, e)
      & maxSCC == $_(sccMap->get(*head))
      & r == $_((*e)->rule())
      & is_recursive == $_(r->recursive.any()); // Sort non-recursive before recursive

    // TODO: mutable lambda directly in query?
    $_(times.emplace_back(*t, *maxSCC, r.get(), *is_recursive), &times) << when(t, maxSCC, is_recursive, r);

    stratifier.manual_stratify({
        1, // deps

        2, // reach

        2, // negative stratification condition, sccMap

        2, // whenAll

        2, // set recursive, when

        1, // times
      });
  }
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
  std::tie(last_time, last_scc, std::ignore, std::ignore) = times[0];
  for (size_t i = 0; i<times.size(); i++) {
    std::tie(time, scc, rule, is_recursive) = times[i];
    
    std::cerr << "STRATIFY: time=" << time << " scc=" << scc << " rule=" << rule-rules.data() << "\n";
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
  Rule *last = q->rules.data();
  for (auto count : counts) {
    q->add_stratum({last, count});
    auto const& to_merge = q->strata.back().to_merge;
    for ( ; count; --count, ++last) {
      for (auto i=1; i<last->size(); ++i) {
        auto dep = q->get_meta(i+last->head).dep;
        if (dep && std::find(to_merge.begin(), to_merge.end(), dep) != to_merge.end())
          last->recursive.set(i);
      }
    }
  }
  assert(q->rules.data() + q->rules.size() == last);
}

void Query::add_stratum(detail::span<Rule> extent)
{
  std::vector<dependency*> to_merge;
  for (auto const& r : extent) {
    auto c = get_meta(r.head).dep;
    if (c && !dynamic_cast<dummy_dep*>(c))
      to_merge.push_back(c);
  }
  std::sort(to_merge.begin(), to_merge.end());
  to_merge.erase(std::unique(to_merge.begin(), to_merge.end()), to_merge.end());
  strata.push_back(stratum{extent, std::move(to_merge)});
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

    // Nonrecursive rules come first
    auto beg = srules.extent.begin(), end = srules.extent.end();
    for ( ; beg != end ; ++beg) {
      Rule& r = *beg;
      if (r.recursive.none())
        run_rule(r, 0);
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
    for (int iter = 0; changed; iter++) {
      DEBUG_PROBE(BREAK_FIXPOINT);
      for (auto it = beg; it != end; ++it) {
        Rule& r = *it;
        assert(r.recursive.any()); // Only recursive rules!
        for (int i=1; i<r.size(); i++) {
          if (r.recursive.test(i))
            run_rule(r, i);
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
