#include <flat/memory>
#include "flat/pnr_utils.h"

#define TRACE_CONTEXT "MEMORY"
TRACE(POOL)
#undef TRACE_CONTEXT
namespace flat {

size_t cache::epoch = 0;
void cache::invalidate_all() noexcept { ++epoch; }
void cache::invalidate(const stamp& s) const noexcept
{
  owner.reset(s);
  memset(buf, 0, MAX_SIZE);
}

autorelease *autorelease::current = nullptr;

void autorelease::clear()
{
  TRACE_POOL << "Clearing pool [" << this->name << "] with "
             << pool.size() << " objects, total bytes = " << total <<"\n";
  for (auto e : pool) {
    if (e.destroy)
      (*e.destroy)(e.p);
    ::operator delete (e.p); // Free memory. Can be removed in a later paged-pool version
  }
  pool.clear();
}

autorelease::~autorelease()
{
  clear();
}
autorelease::autorelease(autorelease&& o) = default;
autorelease::autorelease(std::string&& name): name(std::forward<std::string>(name))
{
  TRACE_POOL << "Created pool [" << this->name << "]\n";
}

autorelease::scoped::scoped(autorelease& pool)
{
  guard::set(&autorelease::current, &pool);
}

}
