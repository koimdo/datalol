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

    std::cout << "As: " << As << "\n";
    std::cout << "FAs: " << FAs << "\n";
    Var<A> a("a");
    Var<int> i("i"), k("k");
    THUNK((results.emplace_back(*i, *k)), &results) << As(a) /*& THUNK(a->i + a->k) == i */ & THUNK(a->j) == i & k == THUNK(a->k) /*& GUARD(*i >= 3)*/;
  }
  //std::cout << qo.to_json();
  ASSERT_EQ(results.size(), 4);
}

TEST(Trivial, reachable) {
  using edges_t = flat::set<std::tuple<int, int>>;
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
    THUNK((answer.insert({*u, *v})), &answer) << Reachable(u, v);
  }

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

class TriangleTest : public ::testing::Test {
protected:
  static flat::set<std::tuple<int, int>> edges;
  using result_t = flat::set<std::tuple<int, int, int>>;

  static result_t result;

  static constexpr int NUM_NODES = 200;

  static void SetUpTestSuite()
  {

    std::random_device rd;
    std::mt19937 gen(rd());
    // give "true" 1/4 of the time
    // give "false" 3/4 of the time
    std::bernoulli_distribution d(0.25);

    for (int i=1; i<=NUM_NODES; i++)
      for (int j=1; j<=NUM_NODES; j++)
        if (d(gen))
          edges.insert({i, j});
    nested_hand(result);
  }

  static void nested_hand(result_t& res)
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
      }
    }
  }
};

flat::set<std::tuple<int, int>> TriangleTest::edges;
TriangleTest::result_t TriangleTest::result;

TEST_F(TriangleTest, hand) {
  result_t myres;
  nested_hand(myres);
  ASSERT_EQ(myres.size(), result.size());
}

TEST_F(TriangleTest, noinline_hand) {
  result_t myres;
  int a, b, c;

  auto head = [&a, &b, &c, &myres]() __attribute__((noinline)) {
    myres.insert({a,b,c});
  };
  auto lca = [&a, &b, &c, &head]() __attribute__((noinline)) {
    if (edges.contains({c, a}))
      head();
  };
  auto guard = [&a, &b, &c, &lca]() __attribute__((noinline)) {
    if (a != b && b != c && a != c)
      lca();
  };
  auto lbc = [&a, &b, &c, &guard]() __attribute__((noinline)) {
    for (auto const& bc : edges) {
      auto b1 = std::get<0>(bc);
      c = std::get<1>(bc);
      if (b != b1)
        continue;
      guard();
    }
  };

  auto lab = [&a, &b, &c, &lbc]() __attribute__((noinline)) {
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

TEST_F(TriangleTest, reachable) {
  DATALOL(reachability) {
    using namespace datalol;
    auto E = external(edges, "edges");
    table<int, int> Reachable("Reachable");
    Var<int> u("u"), v("v"), w("w");
    Reachable(u, v) << E(u, v);
    Reachable(u, w) << E(u, v) & Reachable(v, w);
  }
}

TEST_F(TriangleTest, DISABLED_wcoj) {
  result_t myres;
  DATALOL(triangles) {
    TRIANGLE_QUERY();
    //triangles.set_policy(Query::WCOJ);
  }
  ASSERT_EQ(myres.size(), result.size());
}
