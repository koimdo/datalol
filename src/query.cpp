#include <datalol/syntax.h>
#include <datalol/query.h>
#include <datalol/debug.h>

bool Query::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

static
void verify_neg(const Rule::vars_t& bound, const Rule& r, const Rule::with_vars& e)
{
  auto neg = e.negative;
  neg &= ~bound;
  assert(neg.none());
}

Rule::elem_meta& Query::get_meta(unsigned i) { return elems[i].first; }
Rule::Elem& Query::get_elem(unsigned i) { return *elems[i].second; }


// FIXME: move to builder?
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
        r.seminaive_current = i;
      }
    }
  }

  // Step 3: set undo variables
  for (auto& r : rules) {
    Rule::vars_t bound;
    std::vector<Var_>& stack = r.undo_stack;
    stack.reserve(vars.size());
    for (size_t i=r.head+1; i<r.last; ++i) {
      auto vars = get_meta(i).vars;
      auto pos = vars.positive;
      verify_neg(bound, r, vars);

      pos &= ~bound;
      if (pos.any()) {
        auto& elem = static_cast<Rule::Body&>(get_elem(i));
        elem.undo_vars = stack.data() + stack.size();
        for (auto v : this->vars)
          if (pos.test(v.get_id()))
            stack.push_back(v);
        elem.undo_count = (stack.data() + stack.size()) - elem.undo_vars;
      }

      bound |= pos;
    }

    verify_neg(bound, r, get_meta(r.head).vars);
  }

  // Final step: chain rule body (and head) for execution
  for (auto& r : rules) {
    auto next = &get_elem(r.head);
    next->rule_ = &r;
    for (size_t i=r.last-1; i!=r.head; i--) {
      auto& e = static_cast<Rule::Body&>(get_elem(i));
      e.next_ = next;
      next = &e;
      e.rule_ = &r;
    }
  }
  DEBUG_PROBE(BREAK_CONFIGURE);
}

void Query::run_rule(Rule& r, size_t current_delta)
{
  r.seminaive_current = current_delta;
  auto start = r.head+1;
  r.idx = start;
  get_elem(start).eval();
}


void Query::explain(const std::string& coll, const void *target)
{

}

void Query::run()
{
  size_t changed = 1;
  for (int iter = 0; changed; iter++) {
    DEBUG_PROBE(BREAK_FIXPOINT);
    std::cerr << "Fixpoint iter " << iter << "\n";
    for (auto& r : rules) {
      if (!r.seminaive_current) {
        run_rule(r, 0);
      } else {
        for (size_t i=r.head+1; i<r.last; ++i) {
          if (recursive.test(i))
            run_rule(r, i);
        }
      }
    }
    changed = 0;
    for (auto c : to_merge)
      changed += c->merge();
    std::cerr << "Changed: " << changed << "\n";
  }
  DEBUG_PROBE(BREAK_END);
  dbg->tripcount++;
}


