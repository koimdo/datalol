#define DATALOL_SHORT_THUNK
#include "test_common.h"

#include <datalol/debug-internal.h>

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

static
std::initializer_list<std::tuple<int, int, datalol::lattice::lmin<double>>> apsp_expected = {
  {1, 2, 1.0},
  {1, 3, 3.0},
  {1, 4, 7.0},
  {1, 5, 9.0},
  {1, 6, 8.0},
  {2, 3, 2.0},
  {2, 4, 6.0},
  {2, 5, 8.0},
  {2, 6, 7.0},
  {3, 3, 0.0},
  {3, 4, 4.0},
  {3, 5, 6.0},
  {3, 6, 5.0},
  {4, 5, 2.0},
  {4, 6, 1.0},
  {5, 6, 2.0},
};

TEST(Debug, apsp) {
  using edges_t = std::vector<std::tuple<int, int, double>>;
  edges_t edges = {
    {1, 2, 1.0},
    {1, 2, 4.0},
    {2, 3, 2.0},
    {3, 3, 0.0},
    {3, 4, 4.0},
    {4, 5, 2.0},
    {5, 6, 2.0},
    {4, 6, 1.0},
  };

  using lmin = datalol::lattice::lmin<double>;
  auto answer = DATALOL(apsp) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int, lmin> Shortest("shortest");
    Var<int> u("u"), v("v"), w("w");
    LVar<lmin> d("d"), d1("d1"), d2("d2");

    Shortest(u, w, d) << Shortest(u, v, d1) & E(v, w, d2) & d == d1 + d2;
    Shortest(u, v, d) << E(u, v, d);
    Shortest(u, v, lattice::bot) << E(u, v, ignore);

    return Shortest;
  };

  using value_t = std::tuple<int, int, lmin>;
  std::vector<value_t> contents{answer.begin(), answer.end()};
  ASSERT_EQ(contents, std::vector<value_t>{apsp_expected});
}

template<typename T>
class newtype {
  T t;
public:
  template<typename U>
  newtype(U&& u): t(std::forward<U>(u)) {}
  inline friend std::ostream& operator<<(std::ostream& os, const newtype& t)
  {
    return os << t.t;
  }
  bool operator==(const newtype& o) const { return t == o.t; }
  bool operator<(const newtype& o) const { return t < o.t; }
  const T* operator->() const noexcept { return &t; }
  const T& operator*() const noexcept { return t; }
  T* operator->() noexcept { return &t; }
  T& operator*() noexcept { return t; }
};
#define NEWTYPE(alias, contents) struct alias : public newtype<contents> { using newtype<contents>::newtype; }
TEST(Debug, vpt) {
  NEWTYPE(var_t, std::string);
  NEWTYPE(field_t, std::string);
  NEWTYPE(objid_t, int);

  // Figure 1 from Souffle's provenance paper
  std::vector<std::tuple<var_t, objid_t>> new_op_; // v = oid
  std::vector<std::tuple<var_t, var_t>> assign_; // v1 = v2
  std::vector<std::tuple<var_t, field_t, var_t>> store_; // v.f = v2
  std::vector<std::tuple<var_t, var_t, field_t>> load_; // v = v2.f

  do {
    var_t a("a"), b("b"), c("c"), d("d"), e("e");
    field_t f("f");

    new_op_.emplace_back(a, 1);
    assign_.emplace_back(b, a);
    new_op_.emplace_back(c, 3);
    new_op_.emplace_back(d, 4);
    store_.emplace_back(c, f, a);
    load_.emplace_back(e, d, f);
    load_.emplace_back(b, c, f);
    assign_.emplace_back(a, b);
  } while(0);

  auto answer = DATALOL(pointsto) {
    using namespace datalol;
    auto load = external(load_, "load");
    auto store = external(store_, "store");
    auto new_ = external(new_op_, "new");
    auto assign = external(assign_, "assign");

    table<var_t, objid_t> vpt("vpt");
    table<var_t, var_t> alias("alias");
    Var<var_t> var("var"), var2("var2"), y("y"), p("p"), q("q");
    Var<objid_t> obj("obj"), obj2("obj2");
    Var<field_t> f("f");

    vpt(var, obj) << new_(var, obj);

    vpt(var, obj) << assign(var, var2) & vpt(var2, obj);

    vpt(var, obj) << load(var, y, f) & store(p, f, q) &
                     vpt(q, obj) & vpt(p, obj2) & vpt(y, obj2);

    alias(var, var2) << vpt(var, obj) & vpt(var2, obj) & $_(*var != *var2);

    return vpt;
  };
}

Json::Value operator "" _json(const char *str, std::size_t len)
{
  std::istringstream is(std::string(str, len));
  Json::Value v;
  is >> v;
  return v;
}

class Agent : public testing::Test {
protected:
  void SetUp() override {
    int sockets[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    parent_fd = sockets[0];
    child_fd = sockets[1];

    pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
      // Child process: use child_fd as-is, set env var, exec test
      close(parent_fd);

      // Set environment variable for debug protocol using the socket fd
      char fd_str[16];
      snprintf(fd_str, sizeof(fd_str), "%d", child_fd);
      setenv("LOLBERT_FD", fd_str, 1);

      // Execute the test binary with filter for apsp
      char exe_path[4096];
      ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
      ASSERT_GT(len, 0);
      exe_path[len] = '\0';

      const char *argv[] = {exe_path, "--gtest_filter=Debug.apsp", nullptr};
      execv(exe_path, const_cast<char *const *>(argv));

      // If exec fails, exit with error
      _exit(127);
    } else {
      // Parent process: close child_fd, set up JsonPipe
      close(child_fd);
      pipe.set(parent_fd);
    }
  }

  void TearDown() override {
    if (pipe.fd >= 0) {
      close(pipe.fd);
    }
    if (pid > 0) {
      // Wait for child to finish
      int status;
      waitpid(pid, &status, 0);
    }
  }

  // Read a message from the pipe
  Json::Value read() {
    Json::Value v;
    EXPECT_TRUE(pipe.read(v));
    return v;
  }

  // Send a message to the pipe
  void write(const Json::Value& v) {
    pipe.write(v);
  }

  // Wait for and verify the hello notification
  void read_hello() {
    auto msg = read();
    ASSERT_EQ(msg, R"J({"method": "hello", "params": null})J"_json);
  }

  // Send a request and read the response
  Json::Value request_response(const char *method, int id, const Json::Value& params) {
    Json::Value req;
    req["method"] = method;
    req["id"] = id;
    req["params"] = params;
    pipe.write(req);
    auto resp = read();
    if (!req["id"].isNull())
      EXPECT_EQ(resp["id"], req["id"]);
    EXPECT_EQ(resp.isMember("result"), true);
    return resp["result"];
  }

  // Load queries and return the result
  Json::Value load_queries(int id) {
    auto resp = request_response("loadQueries", id, Json::nullValue);
    EXPECT_TRUE(resp.isObject());
    return resp;
  }

  // Find a query by name, return its id
  int find_query_id(const char* name, const Json::Value& queries_result) {
    for (const auto& q : queries_result["data"]) {
      if (q.isArray() && q.size() >= 2 && q[1].asString() == name)
        return q[0].asInt();
    }
    return -1;
  }

  // Set a breakpoint on a query
  Json::Value set_break(int qid, int flags, int id) {
    Json::Value params;
    params["qid"] = qid;
    params["flags"] = flags;
    auto resp = request_response("set_break", id, params);
    EXPECT_EQ(resp["flags"].asInt(), flags);
    return resp;
  }

  // Resume execution
  void resume() {
    Json::Value req;
    req["method"] = "resume";
    req["params"] = Json::nullValue;
    pipe.write(req);
  }

  // Wait for a breakpoint notification
  Json::Value wait_breakpoint() {
    auto msg = read();
    EXPECT_EQ(msg["method"].asString(), "breakpoint");
    return msg["params"];
  }

  // Show the current query
  Json::Value show_query(int id) {
    return request_response("show_query", id, Json::nullValue);
  }

  // Find a collection by name in show_query result, return its index
  int find_collection_index(const char* name, const Json::Value& show_query_result) {
    int idx = 0;
    for (const auto& coll : show_query_result["db"]["data"]) {
      if (coll.isArray() && coll.size() >= 1 && coll[0].asString() == name)
        return idx;
      idx++;
    }
    return -1;
  }

  // Get table contents by collection index
  Json::Value get_table(int coll_idx, int id) {
    Json::Value params;
    params.append(coll_idx);
    return request_response("get_table", id, params);
  }

  pid_t pid = -1;
  int parent_fd = -1;
  int child_fd = -1;
  datalol::detail::JsonPipe pipe;
};

TEST_F(Agent, hello_handshake) {
  read_hello();
}

TEST_F(Agent, list_queries) {
  read_hello();
  auto result = load_queries(1);

  // Check that the result contains the apsp query
  ASSERT_TRUE(result["data"].isArray());
  EXPECT_GE(find_query_id("apsp", result), 0) << "Expected to find 'apsp' query in the query list";
}

TEST_F(Agent, breakpoint_and_results) {
  read_hello();
  auto queries_result = load_queries(1);

  int apsp_qid = find_query_id("apsp", queries_result);
  ASSERT_GE(apsp_qid, 0);

  set_break(apsp_qid, 4, 2); // BREAK_END

  resume();

  auto bp_params = wait_breakpoint();
  EXPECT_EQ(bp_params["qid"].asInt(), apsp_qid);
  EXPECT_EQ(bp_params["pos"].asInt(), 4); // BREAK_END

  auto show_result = show_query(3);
  int shortest_idx = find_collection_index("shortest", show_result);
  ASSERT_GE(shortest_idx, 0);

  auto table_result = get_table(shortest_idx, 4);

  // Build expected results
  Json::Value expected_data = Json::arrayValue;
  for (auto const& t : apsp_expected) {
    Json::Value elem = Json::arrayValue;
    std::ostringstream lmin_repr;
    lmin_repr << "lmin(" << std::get<2>(t).reveal() << ")";
    elem.append(std::get<0>(t));
    elem.append(std::get<1>(t));
    elem.append(lmin_repr.str());
    expected_data.append(elem);
  }

  ASSERT_TRUE(table_result["data"].isArray());
  ASSERT_EQ(table_result["data"], expected_data);
}

TEST_F(Agent, DISABLED_explain_result) {
  read_hello();
  auto queries_result = load_queries(1);

  int apsp_qid = find_query_id("apsp", queries_result);
  ASSERT_GE(apsp_qid, 0);

  set_break(apsp_qid, 4, 2); // BREAK_END

  resume();

  auto bp_params = wait_breakpoint();
  EXPECT_EQ(bp_params["qid"].asInt(), apsp_qid);
  EXPECT_EQ(bp_params["pos"].asInt(), 4); // BREAK_END

  auto show_result = show_query(3);
  int shortest_idx = find_collection_index("shortest", show_result);
  ASSERT_GE(shortest_idx, 0);

  auto table_result = get_table(shortest_idx, 4);

  Json::Value explain_params;
  explain_params.append(shortest_idx);
  explain_params.append(4);
  auto next_level = request_response("explain", 5, explain_params);

  ASSERT_TRUE(next_level.isObject());
  //ASSERT_EQ(next_level["provenance"])
}
