#include <string>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

#include "datalol/debug.h"

extern struct debug_info  __start_info[];
extern struct debug_info  __stop_info[];

#define DEBUG_ENV_VAR "LOLBERT_FD"
static
int get_debug_fd()
{
  const char *fdnum = getenv(DEBUG_ENV_VAR);
  if (!fdnum || !fdnum[0])
    return -1;
  char *endptr;
  int resfd = strtol(fdnum, &endptr, 10);
  if (*endptr)                  // some non-number content
    return -1;
  struct stat dummy;
  if (fstat(resfd, &dummy))
    return -1;
  return resfd;
}

struct Stubs {
  int fd;

  Stubs() {
    fd = get_debug_fd();
    int nstubs = 0;
    std::cout << "Available queries:\n";
    for (const debug_info *d = __start_info; d != __stop_info; ++d) {
      std::cout << nstubs << "@ [" << d->function << "] "  << d->file << ":" << d->line << "\n";
      nstubs++;
    }
    std::cout << "-----\n";
  }
  ~Stubs() {
    std::cout << "Trip counts:\n";
    for (const debug_info *d = __start_info; d != __stop_info; ++d) {
      std::cout << d->file << ":" << d->line << " " << d->tripcount << " times\n";
    }
    std::cout << "-----\n";
  }

  std::string readbuf;

  const std::string *read()
  {
    char c;
    readbuf.clear();
    for (;;) {
      // TODO: read more than 1 byte with `poll`
      int res = ::read(fd, &c, 1);
      if (res > 0) {
        if (!c)
          return &readbuf;
        else
          readbuf.push_back(c);
      } else if (!res) {
        return nullptr;
      }
    }
  }
};

static Stubs stubs{};

bool is_debug() noexcept
{
  return stubs.fd >= 0;
}
