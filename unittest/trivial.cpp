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
    Var<int> i("i"), k("k");
    THUNK((results.emplace_back(*i, *k)), &results) << As(a) /*& THUNK(a->i + a->k) == i */ & THUNK(a->j) == i & k == THUNK(a->k) /*& GUARD(*i >= 3)*/;
  }
  //std::cout << qo.to_json();
  ASSERT_EQ(results.size(), 4);
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
  }

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
  edges_t answer;

  DATALOL(apsp) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int, double> Reachable("Reachable");
    table<int, int, double> Shortest("shortest");
    Var<int> u("u"), v("v"), w("w");
    Var<double> d("d"), d1("d1"), d2("d2");
    THUNK((answer.push_back({*u, *v, *d})), &answer) << Shortest(u, v, d);

    Reachable(u, w, d) << Reachable(u, v, d1) & E(v, w, d2) & d == $_(*d1 + *d2);
    Reachable(u, v, d) << E(u, v, d);

    Shortest(u, v, d) << Reachable(u, v, d1) & d == aggregate(min($_(*d1)), u, v);
  }

  std::sort(answer.begin(), answer.end());

  edges_t expected = {
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

  ASSERT_EQ(answer, expected);
}
TEST(Trivial, iterate) {
  std::vector<int> result, answer, input = {2, 3, 5};
  DATALOL(squares) {
    using namespace datalol;
    Var<int> n, res;
    auto N = external(input, "input");
    $_(result.push_back(*res), &result) << N(n) & res == iterate($_(xrange(*n, (*n)*(1+*n), *n)));
  }
  answer = {2, 4, 3, 6, 9, 5, 10, 15, 20, 25};
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
  }
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

  static constexpr int NUM_NODES = 200;

  static void SetUpTestSuite()
  {

    std::random_device rd;
    std::mt19937 gen(rd());
    // give "true" 1/4 of the time
    // give "false" 3/4 of the time
    std::bernoulli_distribution d(0.25);

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
    triangles.set_policy(Query::NESTED);
  }
  ASSERT_EQ(myres.size(), result.size());
}

TEST_F(TriangleTest, almost_triangle) {
  result_t myres;
  DATALOL(triangles) {
    using namespace datalol;
    Var<int> a("a"), b("b"), c("c");
    auto E = external(edges, "edges");

    THUNK((myres.insert({*a, *b, *c})), &myres) << E(a, b) & E(b, c) & $_(*a != *b && *b != *c && *a != *c) & !E(c, a);
  }
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
  }
  ASSERT_GT(answer.size(), 5000);
}

TEST_F(TriangleTest, DISABLED_wcoj) {
  result_t myres;
  DATALOL(triangles) {
    TRIANGLE_QUERY();
    //triangles.set_policy(Query::WCOJ);
  }
  ASSERT_EQ(myres.size(), result.size());
}
