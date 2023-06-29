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
print_raw() { return std::make_unique<raw_result>(); }

void select(DQuery&& qf)
{
  std::cout << "SELECT(" << (const Query&)qf << "):\n";
  qf.configure();
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
  
  Var<int> x("x");
  Var<int> y("y");

  select(DQuery{{print_raw() << R(1, 2, 3)}});
  select(DQuery{{print_raw() << R(1, x, y)}});
  select(DQuery{{print_raw() << R(1, x, x)}});
  select(DQuery{{print_raw() << R(1, x, 0)}});
  select(DQuery{{print_raw() << R(x, x, 3)}});

  Var<std::string> s("s");
  select(DQuery{{print_raw() << S(s, y, s)}});

  auto& As = db.objects<A>("AS");

  As.insert(0, 1, 2);
  As.insert(1, 2, 3);
  As.insert(1, 1, 3);
  As.insert(1, 8);
  std::cout << As << "\n";

  // select(As( $(&A::i) == 1, $(&A::k) == 3, $(&A::j) == x));
  select(DQuery{{
        HEAD_WITH(std::cout << y << " " << s << "\n") <<
        R(1, y, 3) &
        S(s, y, s) &
        GUARD(s->size() > 3)
      }});

  // select(DQuery{{
  //       Reachable(x, y) << E(x, y),
  //       Reachable(x, z) << Reachable(x, y) & E(y, z)
  //     }});

  
}
