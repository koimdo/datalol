#include <datalol/syntax.h>
#include <datalol/query.h>

cow_buf::~cow_buf() { clear(); }
void cow_buf::clear()
{
  if (!p)
    return;
  if (destroy)
    (destroy)(p);

  p = nullptr;
  destroy = nullptr;
}

Var_::Var_(const std::string& name)
  : impl(flat::allocate<Impl>())
{
  impl->name = name;
}

bool DQuery::cmp::operator()(Collection_base *l, Collection_base *r) const
{
  return l->get_name() < r->get_name();
}

void DQuery::configure()
{
  for (auto& r : rules) {
    assert(!r->get_body().empty());
    assert(r->get_head());
    query_fragment *next = this;
    auto c = r->get_head()->collection();
    if (c)
      to_merge.insert(c);
    auto body = r->get_body();
    for (int i=body.size()-1; i >= 0; i--) {
      query_fragment *elem = static_cast<query_fragment*>(body[i]);
      elem->next = next;
      next = elem;
    }
  }
}
void DQuery::run() {
  size_t changed ;
  int iter = 0;
  do {
    std::cerr << "Fixpoint iter " << iter << "\n";
    for (auto& r : rules) {
      for (size_t i=0; i<=r->size(); ++i) {
        // FIXME: i<=size() is a dumb hack to allow EDB-only bodies to run
        // Use reachability and SCCs instead.
        auto coll = i < r->size() ? r->get_body()[i]->collection() : nullptr;
        if (coll && !to_merge.contains(coll))
          continue;
        r->seminaive_current = i;
        r->get_body()[0]->eval_body(*r, 0);
      }
    }
    changed = 0;
    for (auto c : to_merge)
      changed += c->merge();
    ++iter;
    std::cerr << "Changed: " << changed << "\n";
  } while (changed);
}

void DQuery::eval_body(Rule& r, size_t)
{
  r.get_head()->eval_head(r);
}

void DQuery::print(std::ostream& os) const { Query::print(os); }


head::head(fun_t&& f, const std::string& desc): f(f), desc(desc) {}
void head::eval_head(Rule&) { f(); }
void head::print(std::ostream& os) const { os << "head(" << desc << ")"; }

guard::guard(fun_t&& f, const std::string& desc): f(f), desc(desc) {}
void guard::eval_body(Rule& r, size_t idx)
{
  // TODO: bind vars in guards?
  if (f()) next->eval_body(r, idx+1);
}
void guard::print(std::ostream& os) const { os << "guard(" << desc << ")"; }
