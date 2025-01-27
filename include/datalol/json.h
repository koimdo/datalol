#pragma once

#include <json/json.h>

namespace datalol {
namespace detail {

template<typename T>
struct json_of;

#define BASIC_TYPE(typ)                                                 \
  template<>                                                            \
  struct json_of<T> {                                                   \
  static Json::Value disp(typ value) { return Json::Value(value); }     \
  }

BASIC_TYPE(Json::Int);
BASIC_TYPE(Json::UInt);
BASIC_TYPE(Json::Int64);
BASIC_TYPE(Json::UInt64)
BASIC_TYPE(double);
BASIC_TYPE(const char *);
BASIC_TYPE(const Json::String&);
BASIC_TYPE(bool);
#undef BASIC_TYPE

template<typename T>
Json::Value
json_of(const T&) {
  std::ostringstream s;
  s << "<Abstract " << ident::make<T>().type_name() << ">";
  return Json::Value(s.str());
}

template<typename T>
Json::Value
json_of(const T* t) {
  std::ostringstream s;
  s << "<Abstract " << ident::make<T>().type_name() << " @ " << static_cast<const void*>(t) << ">";
  return Json::Value(s.str());
}

template<typename T>
typename std::enable_if<is_printable<T>::value, Json::Value>::type
json_of(const T& t) {
  std::ostringstream s;
  s << t;
  return Json::Value(s.str());
}

struct generic_json {
  Json::Value& vec;
  template<typename T>
  bool operator () (int i, T const &v)
  {
    vec.append(json_of(v));
    return true;
  }
};

template<typename... Args>
Json::Value json_of(const std::tuple<Args...>& t)
{
  Json::Value res;
  for_each_in_tuple(generic_json{res}, t);
  return res;
};

template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& columns = {})
{
  Json::Value res;
  Json::Value& values = (res["values"] = Json::arrayValue);
  for (auto const& t : coll)
    values << json_of(t);
  if (columns.size()) {
    Json::Value& jcolumns = (res["columns"] = Json::arrayValue);
    for (auto const& col : columns)
      jcolumns << col;
  }
  return res;
}

}
}
