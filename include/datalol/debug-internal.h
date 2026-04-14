#pragma once

#include "span.h"
#include <functional>
//#include <json/json.h>

namespace Json {

struct Value;

}

namespace datalol {
namespace detail {

struct JsonPipe {
  int fd = -1;

  void set(int fd)
  {
    this->fd = fd;
  }

  short wait(short events);
  bool read(Json::Value& v);
  void write(const Json::Value& val);
};

struct Stubs {
  JsonPipe pipe;
  span<debug_info*> all_queries;
  bool exit_mainloop_;

  using action_t = Json::Value (Stubs::*)(const Json::Value&);
  using JVal = Json::Value;
  std::unordered_map<std::string, action_t> methods;

  int get_qid(const debug_info *dbg) const;
  JVal getQuery_(const debug_info& d) const;

  JVal loadQueries(const JVal&);
  JVal loadSingle(const JVal& id);
  JVal listMethods(const JVal&);
  JVal resume(const JVal&);
  JVal set_break(const JVal& v);
  JVal show_query(const JVal&);
  JVal get_table(const JVal& v);


  Stubs(int fd, span<debug_info*> all_queries);
  Stubs(int fd, const std::function<bool(const debug_info*)>& pred);
  Stubs();
  
  void add_method(const char * name, action_t act);
  void exit_();
  void notify(std::string&& method, Json::Value&& value);

  void mainloop();
};
  
} 
}
