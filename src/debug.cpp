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
#include <unordered_map>
#include "datalol/debug.h"
#include "datalol/debug-internal.h"
#include "datalol/syntax.h"
#include "datalol/relation.h" // for detail::span

extern struct ::datalol::detail::debug_info  *__start_query_info;
extern struct ::datalol::detail::debug_info  *__stop_query_info;

namespace datalol {
namespace detail {

static
datalol::detail::span<debug_info*> g_all_queries = {&__start_query_info,  &__stop_query_info};

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

void JsonPipe::wait(short events)
{
  struct pollfd pollme;
  pollme.fd = fd;
  pollme.events = events;
  pollme.revents = 0;
  while (!(pollme.revents & events))
    poll(&pollme, 1, -1);
}

bool JsonPipe::read(Json::Value& v)
{
  int len = 0;
  int pos = 0;
  std::string readbuf;
  for (;;) {
    char hexbuf[5] = {0};
    wait(POLLIN);
    int res = ::read(fd, hexbuf, 4);
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

void JsonPipe::write(const Json::Value& val)
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

using JVal = Json::Value;

// Utility functions for JsonCpp arrays, in the spirit of Qt's QList:
Json::Value& operator<<(Json::Value& arr, Json::Value&& item)
{
  arr.append(std::move(item));
  return arr;
}
Json::Value& operator<<(Json::Value& arr, const Json::Value& item)
{
  return arr << Json::Value(item);
}

Json::Value operator<<(Json::Value&& arr, Json::Value&& item)
{
  arr.append(std::move(item));
  return arr;
}

Json::Value operator<<(Json::Value&& arr, const Json::Value& item)
{
  return std::move(arr) << Json::Value(item);
}

int Stubs::get_qid(const debug_info *dbg) const
{
  auto it = std::find(all_queries.begin(), all_queries.end(), dbg);
  assert(all_queries.end() != it);
  return it - &__start_query_info;
}

JVal Stubs::getQuery_(const debug_info& d) const
  {
    Json::Value query;
    query["id"] = get_qid(&d);
    query["function"] = d.function;
    query["file"] = d.file;
    query["line"] = d.line;
    query["flags"] = d.flags;
    query["tripcount"] = d.flags;
    return query;
  }

  JVal Stubs::loadQueries(const JVal&)
  {
    Json::Value all;
    for (auto const& d : all_queries) {
      all << getQuery_(*d);
    }
    return all;
  }

  JVal Stubs::loadSingle(const JVal& id)
  {
    return getQuery_(*all_queries[id[0].asInt()]);
  }

  JVal Stubs::listMethods(const JVal&)
  {
    Json::Value all;
    for (auto const& kv : methods)
      all << kv.first;
    return all;
  }

  JVal Stubs::resume(const JVal&) {
    exit_();
    return true;
  }

  JVal Stubs::set_break(const JVal& v)
  {
    int id = v["qid"].asInt();
    int flags = v["flags"].asInt();
    all_queries[id]->flags = flags;
    JVal res(v);
    res["flags"] = all_queries[id]->flags;
    return res;
  }

  JVal Stubs::show_query(const JVal&)
  {
    auto q = Query::current.get();
    Json::Value res;
    res["qid"] = get_qid(q->dbg);
    {
      JVal db(Json::objectValue);
      JVal data(Json::arrayValue);
      db["columns"] = JVal() << "name" << "internal" << "type";
      for (auto const& coll: q->db)
        data << coll->to_json();
      db["data"] = std::move(data);
      res["db"] = std::move(db);
    }
    {
      Json::Value rules(Json::arrayValue);
      for (auto const& r: q->rules)
        rules << (JVal() << r.head << r.last);
      res["rules"] = std::move(rules);
    }
    {
      Json::Value elements(Json::arrayValue);
      for (auto const& e: q->elems)
        elements << e->to_json();
      res["elements"] = std::move(elements);
    }
    return res;
  }

  JVal Stubs::get_table(const JVal& v)
  {
    auto coll = Query::current->db.at(v[0].asInt());
    return coll->get_contents();
  }

  void Stubs::add_method(const char * name, action_t act)
  {
    methods.emplace(name, std::move(act));
  }

  Stubs::Stubs(int fd, span<debug_info*> all_queries)
    : all_queries(all_queries)
  {
    if (fd < 0)
      return;
    
    pipe.set(fd);

    add_method("help", &Stubs::listMethods);
    add_method("loadQueries", &Stubs::loadQueries);
    add_method("resume", &Stubs::resume);
    add_method("set_break", &Stubs::set_break);
    add_method("show_query", &Stubs::show_query);
    add_method("get_table", &Stubs::get_table);

    notify("hello", JVal());
    mainloop();
  }
  
  Stubs::Stubs()
    : Stubs(get_debug_fd(), g_all_queries)
  {
  }

  void Stubs::exit_() { exit_mainloop_ = true; }
  void Stubs::notify(std::string&& method, Json::Value&& value)
  {
    JVal message;
    message["method"] = std::move(method);
    message["params"] = std::move(value);
    pipe.write(message);
  }

  
  void Stubs::mainloop() {
    for (exit_mainloop_ = false; !exit_mainloop_; ) {
      Json::Value v;
      if (!pipe.read(v))
        break;
      assert(v.type() == Json::objectValue);
      auto const& vmethod = v["method"];
      assert(vmethod.isString());
      std::string method = vmethod.asString();
      auto it = methods.find(method);
      auto const& id = v["id"];
      JVal response;
      response["id"] = id;
      bool isError = false;
      if (methods.end() == it) {
        JVal error;
        error["message"] = "Method not found: " + method;
        error["code"] = 42;
        response["error"] = std::move(error);
        isError = true;
      } else {
        action_t act = it->second;
        // TODO: errors?
        response["result"] = (this->*act)(v["params"]);
      }
      if (isError || !id.isNull())
        pipe.write(response);
    }
  }

// FIXME: fluid?
static Stubs stubs{};

void debug_break(const debug_info *dbg, debug_flags pos)
{
  JVal brk;
  brk["qid"] = stubs.get_qid(dbg);
  brk["pos"] = pos;
  stubs.notify("breakpoint", std::move(brk));
  stubs.mainloop();
}

}
}

