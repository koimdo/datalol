#pragma once

#include <json/json.h>
#include "span.h"
#include "tuple_util.h"

namespace datalol {
namespace detail {

// Utility functions for JsonCpp arrays, in the spirit of Qt's QList:
Json::Value& operator<<(Json::Value& arr, Json::Value&& item);
Json::Value& operator<<(Json::Value& arr, const Json::Value& item);
Json::Value operator<<(Json::Value&& arr, Json::Value&& item);
Json::Value operator<<(Json::Value&& arr, const Json::Value& item);
  
#define BASIC_TYPE(typ)                                                 \
  static inline                                                         \
  Json::Value json_of(typ value) { return Json::Value(value); }

BASIC_TYPE(Json::Int)
BASIC_TYPE(Json::UInt)
BASIC_TYPE(Json::Int64)
BASIC_TYPE(Json::UInt64)
BASIC_TYPE(double)
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

struct json_convert {
  template<typename T>
  Json::Value operator()(const T& t) const { return json_of(t); }
};

template<typename Coll, typename F = json_convert>
Json::Value get_contents_common(const Coll& coll,
                                span<std::string> column_names = {},
                                const F& convert = json_convert{})
{
  Json::Value res;
  std::vector<std::string> columns;
  if (column_names.empty()) {
    columns = type_name<std::remove_cv_t<typename Coll::value_type>>{}.get();
    column_names = {columns.data(), columns.size()};
  }
  Json::Value& jcolumns = (res["columns"] = Json::arrayValue);
  for (auto const& col : column_names)
    jcolumns << col;

  Json::Value& values = (res["data"] = Json::arrayValue);
  for (auto const& t : coll) {
    auto row = convert(t);
    assert(column_names.size() == row.size());
    values << std::move(row);
  }

  return res;
}

}
}
