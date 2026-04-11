#define DATALOL_SHORT_THUNK
#include "test_common.h"
#include <random>
#include "set"

struct A {
  int i, j, k;
  A(int i, int j, int k): i(i), j(j), k(k) {}
  A(int i, int j): A(i, j, i*j) {}
  bool lolz(int ofs) const { return k >= std::max(i, j + ofs); }
  int get_i() const { return i; }
  const int& get_j() const { return j; }
  friend std::ostream& operator<<(std::ostream& os, const A& a) {
    return os << "A{i=" << a.i << ", j=" << a.j << ", k=" << a.k << "}";
  }
  bool operator<(const A& o) const
  {
    return std::make_tuple(i, j, k) < std::make_tuple(o.i, o.j, o.k);
  }
  bool operator==(const A& o) const
  {
    return std::make_tuple(i, j, k) == std::make_tuple(o.i, o.j, o.k);
  }
};



TEST(Ident, names) {
  using datalol::detail::ident;
  ident i1 = ident::make<int>();
  ident i2 = ident::make<int>("myInt");
  ident i3 = ident::make<A>();
  ident i4 = ident::make<std::pair<A, int>>("troll");

  ASSERT_EQ(i1.type_name(), "int");
  ASSERT_EQ(i2.type_name(), "int");
  ASSERT_EQ(i3.type_name(), "A");
  ASSERT_EQ(i4.type_name(), "std::pair<A, int>");
  ASSERT_EQ(i4.get_name(), "troll");
}

TEST(Span, subs) {
  using datalol::detail::span;
  A whatever[] = {{1, 2}, {2, 3}, {3, 4}};
  span<A> spanA{whatever};

  ASSERT_EQ(spanA.size(), 3);
  ASSERT_EQ(spanA[1], A(2, 3));

  auto span_k = spanA->*&A::k;
  ASSERT_EQ(span_k.size(), 3);
  ASSERT_EQ(span_k[0], 2);
  ASSERT_EQ(span_k[1], 6);
  ASSERT_EQ(span_k[2], 12);
}
TEST(Json, json_of) {
  using datalol::detail::json_of;
  ASSERT_EQ(json_of(3.14), Json::Value(3.14));
  ASSERT_EQ(json_of("fjord"), Json::Value("fjord"));
  Json::Value expected(Json::arrayValue);
  expected.append(3);
  expected.append("fjord");
  expected.append(false);
  ASSERT_EQ(json_of(std::make_tuple(3, "fjord", false)), expected);
}

template<typename T>
std::string to_string(const T& t)
{
  std::ostringstream ss;
  ss << t;
  return ss.str();
}

TEST(Json, get_contents_common) {
  using namespace datalol::detail;
  std::vector<std::tuple<int, std::string, bool>> coll = {
    {6, "foo", true},
    {2, "bar", false},
    {8, "baz", true},
  };

  Json::Value jval1, jval2;
  Json::Value vals{Json::arrayValue};
  vals
    << (Json::Value{Json::arrayValue} << 6 << "foo" << true)
    << (Json::Value{Json::arrayValue} << 2 << "bar" << false)
    << (Json::Value{Json::arrayValue} << 8 << "baz" << true);
  {
    auto& cols = jval1["columns"] = Json::arrayValue;
    cols << "int" << ident::make<std::string>().type_name() << "bool";
    jval1["data"] = vals;
  }
  {
    auto& cols = jval2["columns"] = Json::arrayValue;
    cols << "number" << "astring" << "lol";
    jval2["data"] = vals;
  }
  EXPECT_EQ(jval1, get_contents_common(coll));
  std::string columns[] = {"number", "astring", "lol"};
  EXPECT_EQ(jval2, get_contents_common(coll, columns));
}

TEST(Trivial, reachable_manual) {
  using edges_t = std::vector<std::tuple<int, int>>;
  edges_t edges = {
    {1, 2},
    {2, 3},
    {3, 3},
    {3, 4},
  };
  edges_t answer;

  DATALOL(reachability) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int> Reachable("Reachable");
    Var<int> u("u"), v("v"), w("w");
    Reachable(u, v) << E(u, v);
    Reachable(u, w) << Reachable(u, v) & Reachable(v, w);

    THUNK((answer.push_back({*u, *v})), &answer) << Reachable(u, v);
    reachability.manual_stratify({2, 1});
  };

  std::sort(answer.begin(), answer.end());
  edges_t expected = {
    {1, 2},
    {1, 3},
    {1, 4},
    {2, 3},
    {2, 4},
    {3, 3},
    {3, 4},
  };

  ASSERT_EQ(answer, expected);
}

template<typename T>
struct smarty {
  const T *a;
  smarty(const T* a): a(a) {}
  const T *operator->() const { return a; }
  const T& operator*() const { return *a; }
  bool operator==(const smarty& o) const { return *a == *o.a; }
  bool operator<(const smarty& o) const { return *a < *o.a; }
};

struct smarty_prox : public smarty<A> {
  using smarty<A>::smarty;
  struct arrow_proxy {
    struct lol {
      int jolly;
    };
    lol mylol;
    explicit arrow_proxy(int j) { mylol.jolly = 2*j+1; }
    lol *operator->() { return &mylol; }
  };
  arrow_proxy operator->() const { return arrow_proxy{a->j}; }
};

namespace detail {
template<size_t>
struct get_policy;

template<> struct get_policy<0> { auto get(const A& a) { return a.i; } };
template<> struct get_policy<1> { auto get(const A& a) { return a.j; } };
template<> struct get_policy<2> { auto get(const A& a) { return a.k; } };

}

template<size_t I>
auto get(const A& a) { return detail::get_policy<I>{}.get(a); }

namespace std {

template<>
struct tuple_size<A> : std::integral_constant<size_t, 3> {};

template<> struct tuple_element<0, A> { using type = int; };
template<> struct tuple_element<1, A> { using type = int; };
template<> struct tuple_element<2, A> { using type = int; };

}

TEST(Trivial, test0) {
  flat::set<A> ASs;
  ASs.insert(A{0, 1, 2});
  ASs.insert(A{1, 2, 3});
  ASs.insert(A{1, 1, 3});
  ASs.insert(A{1, 8});

  std::vector<std::tuple<int, int>> results;

  DATALOL (qo) {
    using namespace datalol;
    auto As = external(ASs, "ref");
    auto FAs = external(ASs.filter([](const A&) { return true; }), "value");

    Var<A> a("a");
    Var<int> i("i"), j("j"), k("k");
    THUNK((results.emplace_back(*j, *k)), &results) << As(a, with_elements, ignore, j, k);
  };
  //std::cout << qo.to_json();
  ASSERT_EQ(results.size(), 4);

  std::vector<std::tuple<int, int>> expected = {{1, 2}, {1, 3}, {2, 3}, {8, 8}};
  ASSERT_EQ(results, expected);
}

TEST(Trivial, deref) {
  A a0 = A{0, 1, 2};
  A a1 = A{1, 2, 3};
  A a2 = A{1, 1, 3};
  A a3 = A{1, 8};

  flat::set<smarty<A>> ASs = {&a0, &a1, &a2, &a3};
  int sum_j = 0;
  DATALOL (deref) {
    using namespace datalol;
    auto As = external(ASs, "ref");
    Var<smarty<A>> a("a");
    Var<int> j("j");
    THUNK((sum_j += *j), &sum_j) << As(a) & j == THUNK(a->j);
  };
  ASSERT_EQ(sum_j, 12);
}

TEST(Trivial, DISABLED_deref_proxy) {
  A a0 = A{0, 1, 2};
  A a1 = A{1, 2, 3};
  A a2 = A{1, 1, 3};
  A a3 = A{1, 8};

  flat::set<smarty_prox> ASs = {&a0, &a1, &a2, &a3};
  int sum_j = 0;
  int sum_k = 0;
  DATALOL (deref_proxy) {
    using namespace datalol;
    auto As = external(ASs, "ref");
    Var<smarty_prox> a("a");
    Var<int> j("j"), k("k");
    THUNK((sum_j += *j), &sum_j) << As(a) & j == THUNK(a->jolly);
    THUNK((sum_k += *k), &sum_k) << As(a) & k == THUNK((*a).k);
  };
  ASSERT_EQ(sum_j, 28);
  ASSERT_EQ(sum_k, 16);
}

TEST(Trivial, reachable) {
  using edges_t = std::vector<std::tuple<int, int>>;
  edges_t edges = {
    {1, 2},
    {2, 3},
    {3, 3},
    {3, 4},
  };
  edges_t answer;

  DATALOL(reachability) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int> Reachable("Reachable");
    Var<int> u("u"), v("v"), w("w");
    THUNK((answer.push_back({*u, *v})), &answer) << Reachable(u, v);
    Reachable(u, w) << Reachable(u, v) & Reachable(v, w);
    Reachable(u, v) << E(u, v);
  };

  std::sort(answer.begin(), answer.end());
  edges_t expected = {
    {1, 2},
    {1, 3},
    {1, 4},
    {2, 3},
    {2, 4},
    {3, 3},
    {3, 4},
  };

  ASSERT_EQ(answer, expected);
}

TEST(Trivial, apsp) {
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
  std::vector<value_t> expected = {
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

  std::vector<value_t> contents{answer.begin(), answer.end()};
  ASSERT_EQ(contents, expected);
}

TEST(Trivial, lset) {
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

  using lset = datalol::lattice::lset<flat::set<std::pair<int, double>>>;
  auto answer = DATALOL(apsp) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, lset> outgoing("shortest");
    table<int, size_t, double> res("res"); // source node, #outgoing, AVG(distance)

    Var<int> u("u"), v("v");
    Var<double> d("d"), avg("avg");
    LVar<lset> out("out");
    Var<size_t> size("size");

    outgoing(u, out) << E(u, v, d) & out == $_(std::make_pair(*v, *d));

    res(u, size, avg) << outgoing(u, out) & size == $_(out.reveal().size()) & avg == $_(({
          double asum = 0;
          for (auto const& lol : out.reveal())
            asum += lol.second;
          asum / out.reveal().size();
        }));

    return res;
  };

  using value_t = std::tuple<int, size_t, double>;
  std::vector<value_t> expected = {
    {1, 2, 2.5},
    {2, 1, 2.0},
    {3, 2, 2.0},
    {4, 2, 1.5},
    {5, 1, 2.0},
  };

  std::vector<value_t> contents{answer.begin(), answer.end()};
  ASSERT_EQ(contents, expected);
}

TEST(Trivial, iterate) {
  std::vector<int> result, answer, input = {2, 3, 5};
  DATALOL(squares) {
    using namespace datalol;
    Var<int> n, res;
    auto N = external(input, "input");
    $_(result.push_back(*res), &result) << N(n) & res == iterate($_(xrange(*n, (*n)*(1+*n), *n)));
  };
  answer = {2, 4, 3, 6, 9, 5, 10, 15, 20, 25};
  ASSERT_EQ(result, answer);
}

TEST(Trivial, enumerate) {
  std::vector<int> result, answer, input = {2, 3, 5};
  DATALOL(squares) {
    using namespace datalol;
    Var<int> n, i;
    $_(result.push_back((*n)*(*n)+i), &result) << (tie(i, n) == enumerate($_(xrange(input.data(), input.data()+input.size()), &input)));
  };
  answer = {4, 10, 27};
  ASSERT_EQ(result, answer);
}

TEST(Trivial, iterate_container) {
  struct noncopy_list {
    std::initializer_list<int> l;
    auto begin() const { return l.begin(); }
    auto end() const { return l.end(); }
    noncopy_list(std::initializer_list<int> l): l(l) {}
    noncopy_list(const noncopy_list&) = delete;
    noncopy_list(noncopy_list&&) = default;
    bool operator==(const noncopy_list& o) const
    {
      return l.size() == o.l.size() && std::equal(l.begin(), l.end(), o.l.begin());
    }
  };
  auto l1 = {2, 3, 5};
  auto l2 = {7, 11};
  auto l3 = {13, 17, 19, 23};
  std::vector<noncopy_list> input;
  input.push_back(l1);
  input.push_back(l2);
  input.push_back(l3);
  std::vector<int> result, answer;
  DATALOL(flatten) {
    using namespace datalol;
    Var<noncopy_list> l;
    Var<int> n, res;
    auto In = external(input, "input");
    $_(result.push_back(*res), &result) <<
      // Backward iteration on iterator-like objects
      (l == iterate($_(xrange(input.data()+input.size()-1, input.data()-1, -1), &input)))
      & n == iterate($_(*l))
      & res == $_((*n)*2);
  };
  answer = {26, 34, 38, 46, 14, 22, 4, 6, 10  };
  ASSERT_EQ(result, answer);
}

class TriangleTest : public ::testing::Test {
protected:
  static flat::set<int> nodes;
  static flat::set<std::tuple<int, int>> edges;
  using result_t = flat::set<std::tuple<int, int, int>>;

  static result_t result;
  static result_t almost;

  static constexpr int NUM_NODES = 100;

  static void SetUpTestSuite()
  {

    std::random_device rd;
    std::mt19937 gen(rd());
    // give "true" 1/4 of the time
    // give "false" 3/4 of the time
    std::bernoulli_distribution d(0.1);

    for (int i=1; i<=NUM_NODES; i++)
      nodes.insert(i);

    for (auto i : nodes)
      for (auto j : nodes)
        if (d(gen))
          edges.insert({i, j});
    nested_hand(result, almost);
  }

  static void nested_hand(result_t& res, result_t& almost)
  {
    for (auto const& ab : edges) {
      auto a1 = std::get<0>(ab);
      auto b1 = std::get<1>(ab);
      for (auto const& bc : edges) {
        auto b2 = std::get<0>(bc);
        auto c1 = std::get<1>(bc);
        if (b1 != b2)
          continue;
        if (!(a1 != b1 && b1 != c1 && a1 != c1))
          continue;
        if (edges.contains({c1, a1}))
          res.insert({a1,b1,c1});
        else
          almost.insert({a1,b1,c1});
      }
    }
  }
};

flat::set<int> TriangleTest::nodes;
flat::set<std::tuple<int, int>> TriangleTest::edges;
TriangleTest::result_t TriangleTest::result;
TriangleTest::result_t TriangleTest::almost;

TEST_F(TriangleTest, hand) {
  result_t myres, dummy;
  nested_hand(myres, dummy);
  ASSERT_EQ(myres.size(), result.size());
}

#define NO_INLINE __attribute__((noinline))
TEST_F(TriangleTest, noinline_hand) {
  result_t myres;
  int a, b, c;

  auto head = [&a, &b, &c, &myres]() NO_INLINE {
    myres.insert({a,b,c});
  };
  auto lca = [&a, &b, &c, &head]() NO_INLINE {
    if (edges.contains({c, a}))
      head();
  };
  auto guard = [&a, &b, &c, &lca]() NO_INLINE {
    if (a != b && b != c && a != c)
      lca();
  };
  auto lbc = [&a, &b, &c, &guard]() NO_INLINE {
    for (auto const& bc : edges) {
      auto b1 = std::get<0>(bc);
      c = std::get<1>(bc);
      if (b != b1)
        continue;
      guard();
    }
  };

  auto lab = [&a, &b, &c, &lbc]() NO_INLINE {
    for (auto const& ab : edges) {
      a = std::get<0>(ab);
      b = std::get<1>(ab);
      lbc();
    }
  };

  lab();
  ASSERT_EQ(myres.size(), result.size());
}

#define TRIANGLE_QUERY()                                                \
  using namespace datalol;                                              \
  Var<int> a("a"), b("b"), c("c");                                      \
  auto E = external(edges, "edges");                                    \
                                                                        \
  THUNK((myres.insert({*a, *b, *c})), &myres) << E(a, b) & E(b, c) & $_(*a != *b && *b != *c && *a != *c) & E(c, a)

TEST_F(TriangleTest, nested) {
  result_t myres;
  DATALOL(triangles) {
    TRIANGLE_QUERY();
    triangles.set_policy(execution_policy::NESTED);
  };
  ASSERT_EQ(myres.size(), result.size());
}

TEST_F(TriangleTest, almost_triangle) {
  result_t myres;
  DATALOL(triangles) {
    using namespace datalol;
    Var<int> a("a"), b("b"), c("c");
    auto E = external(edges, "edges");

    THUNK((myres.insert({*a, *b, *c})), &myres) << E(a, b) & E(b, c) & $_(*a != *b && *b != *c && *a != *c) & !E(c, a);
  };
  ASSERT_EQ(myres.size(), almost.size());
}

TEST_F(TriangleTest, reachable) {
  std::vector<std::pair<int, int>> answer;
  DATALOL(reachability) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int> Reachable("Reachable");
    Var<int> u("u"), v("v"), w("w");
    Reachable(u, v) << E(u, v);
    Reachable(u, w) << Reachable(u, v) & E(v, w);

    $_(answer.push_back({*u, *v}), &answer) << Reachable(u, v);
  };
  ASSERT_GT(answer.size(), 5000);
}

TEST_F(TriangleTest, DISABLED_wcoj) {
  result_t myres;
  DATALOL(triangles) {
    TRIANGLE_QUERY();
    //triangles.set_policy(Query::WCOJ);
  };
  ASSERT_EQ(myres.size(), result.size());
}
