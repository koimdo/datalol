#include <datalol/syntax.h>
#include <datalol/query.h>

bool DQuery::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

void DQuery::configure()
{
  // Step 1: figure out relations that are on the HEAD side
  for (auto& r : rules) {
    assert(!r->get_body().empty());
    assert(r->get_head());
    auto c = r->get_head()->collection();
    if (c)
      to_merge.insert(c);

    // Now, daisy-chain the rule body
    Rule::Elem *next = r->get_head().get(flat::unsafe_extract_pointer{});
    auto body = r->get_body();
    for (int i=body.size()-1; i >= 0; i--) {
      Rule::Elem *elem = static_cast<Rule::Elem*>(body[i]);
      elem->next = next;
      next = elem;
    }
  }
  // Step 2: Mark recursions in rule bodies
  // TODO: stratify using SCC on the rule graph.
  for (auto& r : rules) {
    for (size_t i=0; i<r->size(); ++i) {
      auto coll = r->get_body()[i]->collection();
      if (coll && to_merge.contains(coll))
        r->recursive.set(i);
    }
  }
}

void DQuery::run() {
  auto guard = with_query();
  size_t changed = 1;
  for (int iter = 0; changed; iter++) {
    std::cerr << "Fixpoint iter " << iter << "\n";
    for (auto& r : rules) {
      if (r->recursive.none()) {
        r->run(-1);
      } else {
        for (size_t i=0; i<r->size(); ++i) {
          if (r->recursive.test(i))
            r->run(i);
        }
      }
    }
    changed = 0;
    for (auto c : to_merge)
      changed += c->merge();
    std::cerr << "Changed: " << changed << "\n";
  }
}

void DQuery::print(std::ostream& os) const { Query::print(os); }


head::head(fun_t&& f, const std::string& desc)
  : Head(eval_head)
  , f(f)
  , desc(desc)
{}
void head::eval_head(Rule::Elem& self, Rule&, size_t) { static_cast<head&>(self).f(); }
void head::print(std::ostream& os) const
{
  os << "head(" << desc << ")";
}

guard::guard(fun_t&& f, const std::string& desc)
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
