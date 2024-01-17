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

  bool operator==(const A& o) const
  {
    return i == o.i && j == o.j && k == o.k;
  }
};

TEST(Trivial, test0) {
  flat::set<A> ASs;
  
  ASs.insert(A{0, 1, 2});
  ASs.insert(A{1, 2, 3});
  ASs.insert(A{1, 1, 3});
  ASs.insert(A{1, 8});

  std::vector<std::tuple<int, int>> results;

  Query qo;
  DATALOL_Q (qo) {
    // TODO: update external collections on Query::
    auto As = external_ref(ASs);
    auto FAs = external_copy(ASs.filter([](const A&) { return true; }));
    Var<A> a("a");
    Var<int> i("i"), k("k");
    Var<int> param;
    // FIXME: remove the `,true` part once we get the proper typing suppoer for head-only thunks
    THUNK((results.emplace_back(i, k)), &results) << As(a) /*& THUNK(a->i + a->k) == i */ & THUNK(a->j) == i & k == THUNK(a->k) /*& GUARD(*i >= 3)*/;
  }
  //std::cout << qo.to_json();
  qo.run();
  ASSERT_EQ(results.size(), 4);
}
