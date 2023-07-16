#include <datalol/syntax.h>
#include <datalol/query.h>

Rule::vars_t *query_fragment::current_vars = nullptr;
Var_::Var_(const Var_& v)
  : impl(v.impl)
{
  if (query_fragment::current_vars) {
    query_fragment::current_vars->set(Query::current->get_id(impl));
  }
}
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
    query_fragment *next = this;
    auto body = r->get_body();
    for (int i=body.size()-1; i >= 0; i--) {
      query_fragment *elem = static_cast<query_fragment*>(body[i]);
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

void DQuery::eval_body(Rule& r, size_t)
{
  r.get_head()->eval_head(r);
}

void DQuery::print(std::ostream& os) const { Query::print(os); }


head::head(fun_t&& f, const std::string& desc): f(f), desc(desc) {}
void head::eval_head(Rule&) { f(); }
void head::print(std::ostream& os) const { os << "head(" << desc << ")"; }

guard::guard(const Rule::vars_t& vars, fun_t&& f, const std::string& desc): vars(vars), f(f), desc(desc) {}
void guard::eval_body(Rule& r, size_t idx)
{
  if (f()) next->eval_body(r, idx+1);
}
void guard::print(std::ostream& os) const
{
  os << "guard(" << desc << ")[";
  Query::print_vars(os, vars);
  os << "]";
}
