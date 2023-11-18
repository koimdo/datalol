#include <datalol/syntax.h>
#include <datalol/query.h>

bool Query::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

static
void verify_neg(const Rule::vars_t& bound, const Rule& r, const Rule::with_vars& e)
{
  auto neg = e.negative;
  neg &= ~bound;
  if (neg.none())
    return;
  std::cerr << "Error in rule " << r << ": unbound vars" << "\n";
  assert(false);
}

Rule::elem_meta& Query::get_meta(unsigned i) { return elems[i].first; }
Rule::uelem Query::get_elem(unsigned i) { return elems[i].second; }

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
    for (size_t i=r.head+1; i<r.last; ++i) {
      auto vars = get_meta(i).vars;
      auto pos = vars.positive;
      verify_neg(bound, r, vars);

      bool has_vars = pos.any();
      pos &= ~bound;
      auto elem = get_elem(i).cast<Rule::Body>();
      for (auto& v : this->vars)
        if (pos.test(v.get_id())) {
          elem->add_undo(&v);
        }
      if (has_vars)
        elem->add_undo(nullptr);

      bound |= pos;
    }

    verify_neg(bound, r, get_meta(r.head).vars);
  }

  // Final step: chain rule body (and head) for execution
  for (auto& r : rules) {
    auto next = get_elem(r.head);
    for (size_t i=r.last-1; i!=r.head; i--) {
      auto e = get_elem(i).cast<Rule::Body>();
      e->next = next.get(flat::unsafe_extract_pointer{});
      next = e;
    }
  }
}

void Query::run_rule(Rule& r, size_t current_delta)
{
  r.seminaive_current = current_delta;
  get_elem(r.head+1)->eval(r, r.head+1);
}

void Query::run() {
  auto guard = with_query();
  size_t changed = 1;
  for (int iter = 0; changed; iter++) {
    std::cerr << "Fixpoint iter " << iter << "\n";
    for (auto& r : rules) {
      if (!r.seminaive_current) {
        run_rule(r, -1);
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
}


