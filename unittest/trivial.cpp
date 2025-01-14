#include "test_common.h"

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
};

TEST(Trivial, test0) {
  flat::set<A> ASs;
  
  ASs.insert(A{0, 1, 2});
  ASs.insert(A{1, 2, 3});
  ASs.insert(A{1, 1, 3});
  ASs.insert(A{1, 8});

  std::vector<std::tuple<int, int>> results;

  DATALOL (qo) {
    auto As = external(ASs, "ref");
    auto FAs = external(ASs.filter([](const A&) { return true; }), "value");

    std::cout << "As: " << As << "\n";
    std::cout << "FAs: " << FAs << "\n";
    Var<A> a("a");
    Var<int> i("i"), k("k");
    THUNK((results.emplace_back(i, k)), &results) << As(a) /*& THUNK(a->i + a->k) == i */ & THUNK(a->j) == i & k == THUNK(a->k) /*& GUARD(*i >= 3)*/;
  }
  //std::cout << qo.to_json();
  ASSERT_EQ(results.size(), 4);
}

TEST(Trivial, reachable) {
  std::set<std::tuple<int, int>> edges, answer;
  edges.emplace(1, 2);
  edges.emplace(2, 3);
  edges.emplace(3, 3);
  edges.emplace(3, 4);

  DATALOL(reachability) {
    auto E = external(edges, "edges");
    table<int, int> Reachable("Reachable");
    Var<int> u("u"), v("v"), w("w");
    Reachable(u, v) << E(u, v);
    Reachable(u, w) << Reachable(u, v) & Reachable(v, w);
    THUNK((answer.emplace(*u, *v)), &answer) << Reachable(u, v);
  }

  std::set<std::tuple<int, int>> expected = {
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
