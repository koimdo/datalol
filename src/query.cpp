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

void DQuery::configure()
{
  for (auto& r : rules) {
    assert(!r.get_body().empty());
    assert(r.get_head());
    query_fragment *next = this;
    auto body = r.get_body();
    for (int i=body.size()-1; i >= 0; i--) {
      query_fragment *elem = static_cast<query_fragment*>(body[i]);
      elem->next = next;
      next = elem;
    }
  }
}
void DQuery::run() {
  for (auto& r : rules) {
    r.get_body()[0]->eval_body(r);
  }
}

void DQuery::eval_body(Rule& r)
{
  r.get_head()->eval_head(r);
}

void DQuery::print(std::ostream& os) const { Query::print(os); }


head::head(fun_t&& f, const std::string& desc): f(f), desc(desc) {}
void head::eval_head(Rule&) { f(); }
void head::print(std::ostream& os) const { os << "head(" << desc << ")"; }

guard::guard(fun_t&& f, const std::string& desc): f(f), desc(desc) {}
void guard::eval_body(Rule& r)
{
  // TODO: bind vars in guards?
  if (f()) next->eval_body(r);
}
void guard::print(std::ostream& os) const { os << "guard(" << desc << ")"; }
