#include <datalol/pool.h>

namespace datalol {
namespace detail {

pool::pool() = default;
pool::~pool()
{
  for (auto&& e : items) {
    if (e.destroy)
      (*e.destroy)(e.p);
    ::operator delete (e.p); // Free memory. Can be removed in a later paged-pool version
  }
}

}
}
