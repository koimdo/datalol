#include <flat/memory>
#include <cstddef>
#include "flat/pnr_utils.h"

#define TRACE_CONTEXT "MEMORY"
TRACE(POOL)
TRACE(PAGE)
#undef TRACE_CONTEXT

static constexpr size_t PAGE_SIZE = 1<<16; // LOL why so small?

namespace flat {

void cache::invalidate(const stamp& s) const noexcept
{
  owner.reset(s);
}

struct dtor_entry {
  void *p;
  typedef void (*destructor_t)(void*);
  destructor_t destroy;
};

struct autorelease::page : immovable {
  std::vector<dtor_entry> dtors;
  unsigned char *bump;
  size_t nobjects = 0;
  alignas(alignof(std::max_align_t)) unsigned char buf[PAGE_SIZE];
  page(): bump(buf)
  {}

  ~page()
  {
    for (auto e : dtors)
      (e.destroy)(e.p);
  }

  void *try_allocate(guard& undo, size_t size, size_t align)
  {
    TRACE_PAGE << "Allocating " << size << "(align=" << align << ") @ " << bump-buf << ": ";

    size_t avail = PAGE_SIZE-(bump-buf);
    auto intptr = reinterpret_cast<uintptr_t>(bump);

    auto aligned = (intptr + align - 1u) & (-align);
    if (size + (aligned-intptr) > avail) {
      TRACE_PAGE << "Insufficient space!\n";
      return nullptr;
    }

    unsigned char *res = reinterpret_cast<unsigned char*>(aligned);
    undo.set(&bump, res+size);  // In case placement new fails later
    TRACE_PAGE << res-buf << " (bump=" << bump-buf << ")\n";

    return res;
  }
};


autorelease *autorelease::current = nullptr;

// Returns false for pointers that point into a pool in the finalization stage
bool autorelease::is_valid(void *p)
{
  if (!p) return true;          // no problem for NULL pointers.
  return true;                  // TODO: is pointer in a currently-destroyed pool?
}

void autorelease::add_page()
{
  pool.emplace_back();
  pg = &pool.back();
  TRACE_POOL << "Added a page in pool [" << name << "]\n";
}

void *autorelease::allocate_(guard& undo, size_t size, size_t align)
{
  if (size > PAGE_SIZE) {
    TRACE_POOL << "Allocation of " << size << " exceeds PAGE_SIZE!\n";
    return nullptr;
  }

  void *p = pg->try_allocate(undo, size, align);
  if (p)
    return p;

  add_page();
  return pg->try_allocate(undo, size, align);
}

void autorelease::finish_allocation_(void *p, void (*dtor)(void *))
{
  if (dtor)
    pg->dtors.push_back(dtor_entry{p, dtor});
  ++pg->nobjects;
}

void autorelease::clear()
{
  if (TRACE_POOL) {
    size_t nbytes = 0, nobjects = 0, ndtors = 0;
    for (auto const& pg : pool) {
      nbytes += (pg.bump - pg.buf);
      nobjects += pg.nobjects;
      ndtors += pg.dtors.size();
    }
    TRACE_POOL << "Clearing pool [" << this->name << "] with "
               << pool.size() << " pages, objects=" << nobjects
               << " bytes=" << nbytes
               << " dtors=" << ndtors
               <<"\n";
  }
  pool.clear();
}

autorelease::~autorelease()
{
  clear();
}
autorelease::autorelease(std::string&& name): name(std::forward<std::string>(name))
{
  TRACE_POOL << "Created pool [" << this->name << "]\n";
  add_page();
}

autorelease::scoped::scoped(autorelease& pool)
{
  guard::set(&autorelease::current, &pool);
}

}
