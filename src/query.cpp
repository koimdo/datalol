#include <datalol/syntax.h>
#include <datalol/query.h>
#include <datalol/debug.h>
#include <limits>

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
    if (pos.any()) {
      auto& elem = static_cast<Rule::Body&>(get_elem(r.head+ofs));
      elem.undo_vars = stack.data() + stack.size();
      for (auto v : this->vars)
        if (pos.test(v.get_id()))
          stack.push_back(v);
      elem.undo_count = (stack.data() + stack.size()) - elem.undo_vars;
    }

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

  // Step 4: configure leapers for undo stack
  // For var i, the possible leapers are those with i ∈ pos and neg ⊆ {var j : j < i}
  Rule::vars_t valid;
  for (auto v : r.undo_stack) {
    std::vector<Rule::Body*> level;
    for (unsigned i=r.head+1; i<r.last; i++) {
      auto vars = get_meta(i);
      if (vars.positive.test(v.get_id()) && ((vars.negative & valid) == vars.negative))
        level.push_back(&static_cast<Rule::Body&>(get_elem(i)));
    }
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
        r.seminaive_current = i;
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
  Var_ v = r.undo_stack[vidx];
  auto& leapers = r.leapers[vidx];
  size_t min_count = std::numeric_limits<size_t>::max();
  Rule::Body *min_leaper = nullptr;
  for (auto leaper : leapers) {
    auto count = leaper->count();
    if (count < min_count) {
      min_count = count;
      min_leaper = leaper;
    }
  }

  if (!min_count)
    return;

  min_leaper->propose(v);
  for (auto l : leapers) {
    if (l != min_leaper)
      l->intersect(v);
  }

  if (vidx == r.undo_stack.size()-1) {
    auto& head = static_cast<Rule::Head&>(get_elem(r.head));
    for (auto p : v) {
      v.set(p);
      head.eval();
    }
  } else {
    for (auto p : v) {
      v.set(p);
      run_var(r, vidx+1);
      v.zap();
    }
  }
}

void Query::run_rule(Rule& r, size_t current_delta)
{
  // r.seminaive_current = current_delta;
  // auto start = r.head+1;
  // r.idx = start;
  // get_elem(start).eval();
  run_var(r, 0);
}


void Query::explain(const std::string& coll, const void *target)
{

}

void Query::run()
{
  size_t changed = 1;
  for (int iter = 0; changed; iter++) {
    DEBUG_PROBE(BREAK_FIXPOINT);
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
  }
  DEBUG_PROBE(BREAK_END);
  dbg->tripcount++;
}


