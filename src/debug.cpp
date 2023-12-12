#include <string>
#include <iostream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/uio.h>
#include <poll.h>
#include <fcntl.h>
#include <sstream>
#include <json/json.h>
#include <cassert>
#include <flat/span>
#include <flat/map>
#include "datalol/debug.h"

extern struct debug_info  __start_info[];
extern struct debug_info  __stop_info[];
static const flat::span<debug_info> all_queries = {__start_info,  __stop_info};

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
  if (::fstat(resfd, &dummy))
    return -1;

  ::fcntl(resfd, F_SETFL, O_NONBLOCK);
  return resfd;
}

struct JsonPipe {
  int fd = -1;

  void set(int fd)
  {
    this->fd = fd;
  }

  void wait(short events)
  {
    struct pollfd pollme;
    pollme.fd = fd;
    pollme.events = events;
    pollme.revents = 0;
    while (!(pollme.revents & events))
      poll(&pollme, 1, -1);
  }

  // TODO: replace read() and write() with poll-based nonblocking
  // streambuf and let the json decoder do the chunking?
  bool read(Json::Value& v)
  {
    int len = 0;
    int pos = 0;
    std::string readbuf;
    for (;;) {
      char hexbuf[5] = {0};
      wait(POLLIN);
      int res = ::read(fd, hexbuf, 4);
      std::cout << "Read " << res << " bytes\n";
      if (res <= 0)
        return false;           // TODO: error handling?
      char *end;
      len = strtol(hexbuf, &end, 16);
      if (*end)
        return false;
      if (!len)                 // flush packet
        break;
      assert(len > 4);
      len -= 4;
      readbuf.append(len, '\0');
      res = ::read(fd, const_cast<char*>(readbuf.data() + pos), len);
      if (res < 0)
        return false;
    }

    std::istringstream sin(readbuf);
    sin >> v;
    return true;
  }

  void write(const Json::Value& val)
  {
    std::string writebuf;
    {
      std::ostringstream sout;
      sout << val;
      writebuf = sout.str();
    }
    const char *pos = writebuf.data();
    const char *end = writebuf.data() + writebuf.size();
    while (pos < end) {
      int len = std::min(4096L, end-pos);
      char hexbuf[5] = {0};
      snprintf(hexbuf, 5, "%04x", len+4);
      // TODO: error/signal handling
      struct iovec iovecs[] = { {hexbuf, 4}, {const_cast<char*>(pos), (size_t)len}};
      ::writev(fd, iovecs, 2);
      pos += len;
    }
    ::write(fd, "0000", 4);     // flush packet
  }
};

struct Stubs {
  JsonPipe pipe;

  using action_t = Json::Value (Stubs::*)(const Json::Value&);
  flat::map<std::string, action_t> methods;

  using JVal = Json::Value;

  static
  JVal getQuery_(const debug_info& d)
  {
    Json::Value query;
    query["id"] = &d - all_queries.begin();
    query["function"] = d.function;
    query["file"] = d.file;
    query["line"] = d.line;
    query["flags"] = d.flags;
    query["tripcount"] = d.flags;
    return query;
  }

  JVal loadQueries(const JVal&)
  {
    Json::Value all;
    for (auto const& d : all_queries) {
      all.append(getQuery_(d));
    }
    return all;
  }

  JVal loadSingle(const JVal& id)
  {
    return getQuery_(all_queries[id[0].asInt()]);
  }

  JVal listMethods(const JVal&)
  {
    Json::Value all;
    for (auto const& kv : methods)
      all.append(kv.first);
    return all;
  }

  JVal resume(const JVal&) {
    exit_();
    return true;
  }

  JVal set_break(const JVal& v)
  {
    int id = v["query"].asInt();
    int flags = v["flags"].asInt();
    // FIXME: span<T> is not const, while span<const T> is
    const_cast<debug_info&>(all_queries[id]).flags = flags;
    return true;
  }

  Stubs() {
    int fd = get_debug_fd();
    int nstubs = 0;
    std::cout << "DEBUG fd: " << fd << "\n";
    if (fd < 0)
      return;
    
    pipe.set(fd);

    methods["help"] = &Stubs::listMethods;
    methods["loadQueries"] = &Stubs::loadQueries;
    methods["resume"] = &Stubs::resume;

    //notify("Hello", JVal());
    
    mainloop();
  }

  bool exit_mainloop_;
  void exit_() { exit_mainloop_ = true; }
  void notify(std::string&& method, Json::Value&& value)
  {
    JVal message;
    message["method"] = std::move(method);
    message["params"] = std::move(value);
    pipe.write(message);
  }

  void mainloop() {
    for (exit_mainloop_ = false; !exit_mainloop_; ) {
      Json::Value v;
      if (!pipe.read(v))
        break;
      assert(v.type() == Json::objectValue);
      std::cout << "Got JSON: " << v <<  " of type " << v.type() << "\n";
      auto const& vmethod = v["method"];
      assert(vmethod.isString());
      std::string method = vmethod.asString();
      auto it = methods.find(method);
      auto const& id = v["id"];
      JVal response;
      response["id"] = id;
      if (methods.end() == it) {
        JVal error;
        error["message"] = "Method not found: " + method;
        error["code"] = 42;
        response["error"] = std::move(error);
      } else {
        action_t act = it->second;
        // TODO: errors?
        response["result"] = (this->*act)(v["params"]);
      }
      pipe.write(response);
    }
    std::cout << "Exit mainloop\n";
  }
};

static Stubs stubs{};

bool is_debug() noexcept
{
  return stubs.pipe.fd >= 0;
}
