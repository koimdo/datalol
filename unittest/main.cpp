#include <datalol/query.h>

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



struct raw_result : Rule::Head {
  void eval_head(Rule& r) override
  {
    std::cout << r << "\n";
  }
  void print(std::ostream& os) const override
  {
    os << "<debug print>";
  }
};

static
Rule::uhead
print_raw() { return flat::allocate<raw_result>(); }

void select(DQuery&& qf)
{
  std::cout << "SELECT(" << (const Query&)qf << "):\n";
  qf.run();
}

int main()
{
  DB db;
  auto& R = db.table<int, int, int>("R");

  R.insert(1, 2, 3);
  R.insert(1, 2, 3);
  R.insert(1, 1, 3);
  R.insert(0, 2, 0);
  std::cout << R << "\n"; 

  auto& S = db.table<std::string, int, std::string>("S");
  S.insert("Hello", 2, "Hello");
  S.insert("Hello", 2, "Datalog");
  S.insert("LOL", 1, "LOL");
  S.insert("Goodbye", 1, "Query");
  S.insert("Hello", 3, "World");
  std::cout << S << "\n"; 
  
  auto& As = db.objects<A>("AS");

  As.insert(0, 1, 2);
  As.insert(1, 2, 3);
  As.insert(1, 1, 3);
  As.insert(1, 8);
  std::cout << As << "\n";

  auto& res = db.table<std::string, int, std::string>("res");
  // select(As( $(&A::i) == 1, $(&A::k) == 3, $(&A::j) == x));
  auto qo = DATALOL(Var<A> a("a");
                    Var<int> i("i"), j("j"), k;
                    print_raw() << As(a, $_(a->i) == i, k == $_(a->k));
                    );
  select(std::move(qo));

  auto q1 = DATALOL(Var<int> x("x"), y("y");
                    Var<std::string> s("s");
                    int three = 1 + 2;
                    res(s, y, std::string("Bye")) <<
                    R(1, y, three) &
                    S(s, y, s) &
                    GUARD(s->size() > 3);
                    );

  select(std::move(q1));
  std::cout << res << "\n";

  auto& E = db.table<int, int>("E");
  auto& Reachable = db.table<int, int>("Reachable");

  E.insert(1, 2);
  E.insert(2, 3);
  E.insert(3, 3);
  E.insert(3, 4);

  
  auto q2 = DATALOL(Var<int> u("u"), v("v"), w("w");
                    Reachable(u, v) << E(u, v),
                    Reachable(u, w) << Reachable(u, v) & E(v, w)
                    );
  select(std::move(q2));
  
}
