#pragma once

#include <json/json.h>

namespace datalol {
namespace detail {

#define BASIC_TYPE(typ)                                                 \
  static inline                                                         \
  Json::Value json_of(typ value) { return Json::Value(value); }

BASIC_TYPE(Json::Int)
BASIC_TYPE(Json::UInt)
BASIC_TYPE(Json::Int64)
BASIC_TYPE(Json::UInt64)
BASIC_TYPE(double);
BASIC_TYPE(const char *)
BASIC_TYPE(const Json::String&)
BASIC_TYPE(bool)
#undef BASIC_TYPE

template<typename T>
Json::Value json_of(const T&, std::false_type) {
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
Json::Value json_of(const T& t, std::true_type) {
  std::ostringstream s;
  s << t;
  return Json::Value(s.str());
}

template<typename T>
Json::Value json_of(const T& t)
{
  return json_of(t, is_printable<T>{});
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

template<typename T>
struct type_name {
  std::vector<std::string> get() const
  {
    return { ident::make<T>().type_name() };
  }
};

template<typename... Args>
struct type_name<std::tuple<Args...>> {
  std::vector<std::string> get() const
  {
    return { ident::make<Args>().type_name()... };
  }
};

template<typename Coll>
Json::Value get_contents_common(const Coll& coll, const std::vector<std::string>& column_names = {})
{
  Json::Value res;
  Json::Value& values = (res["values"] = Json::arrayValue);
  for (auto const& t : coll)
    values << json_of(t);
  std::vector<std::string> columns;
  if (column_names.empty())
    columns = type_name<typename Coll::value_type>{}.get();
  else {
    assert(column_names.size() == tuple_size<typename Coll::value_type>::value);
    columns = column_names;
  }
  Json::Value& jcolumns = (res["columns"] = Json::arrayValue);
  for (auto const& col : columns)
    jcolumns << col;
  return res;
}

}
}
