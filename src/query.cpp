#include <datalol/syntax.h>
#include <datalol/query.h>

bool Query::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

static
void verify_neg(const Rule::vars_t& bound, const Rule& r, const Rule::Elem& e)
{
  auto neg = e.negative;
  neg &= ~bound;
  if (neg.none())
    return;
  std::cerr << "Error in rule " << r << ": unbound vars" << "\n";
  assert(false);
}
void Query::configure()
{
  // Step 1: figure out relations that are on the HEAD side
  for (auto& r : rules) {
    assert(!r.get_body().empty());
    assert(r.get_head());
    auto c = r.get_head()->collection();
    if (c)
      to_merge.insert(c);
  }
  // Step 2: Mark recursions in rule bodies
  // TODO: stratify using SCC on the rule graph.
  for (auto& r : rules) {
    for (size_t i=0; i<r.size(); ++i) {
      auto coll = r.get_body()[i]->collection();
      if (coll && to_merge.contains(coll))
        r.recursive.set(i);
    }
  }

  // Step 3: set undo variables
  for (auto& r : rules) {
    Rule::vars_t bound;
    int i=0;
    for (auto elem : r.get_body()) {
      auto pos = elem->positive;
      verify_neg(bound, r, *elem);

      if (pos.any())
        elem->add_undo(nullptr);
      pos &= ~bound;
      for (auto v : vars)
        if (pos.test(v->id)) {
          Var_ vv(v);
          elem->add_undo(&vv);
        }
      bound |= pos;
      i++;
    }

    verify_neg(bound, r, *r.get_head());
  }
}

void Query::run() {
  auto guard = with_query();
  size_t changed = 1;
  for (int iter = 0; changed; iter++) {
    std::cerr << "Fixpoint iter " << iter << "\n";
    for (auto& r : rules) {
      if (r.recursive.none()) {
        r.run(-1);
      } else {
        for (size_t i=0; i<r.size(); ++i) {
          if (r.recursive.test(i))
            r.run(i);
        }
      }
    }
    changed = 0;
    for (auto c : to_merge)
      changed += c->merge();
    std::cerr << "Changed: " << changed << "\n";
  }
}

head::head(const std::string& desc, fun_t&& f)
  : Head(eval_head)
  , f(f)
  , desc(desc)
{}
void head::eval_head(Rule::Elem& self, Rule&, size_t) { static_cast<head&>(self).f(); }
void head::print(std::ostream& os) const
{
  os << "head(" << desc << ")";
}

guard::guard(const std::string& desc, fun_t&& f)
  : Rule::Elem(&eval_body)
  , f(f)
  , desc(desc)
{}
void guard::eval_body(Rule::Elem& self_, Rule& r, size_t idx)
{
  guard& self = static_cast<guard&>(self_);
  if (self.f()) self.next->eval(r, idx+1);
}
void guard::print(std::ostream& os) const
{
  os << "guard(" << desc << ")";
}
