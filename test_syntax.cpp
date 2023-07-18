#include "syntax.h"

// ------------------------------------------
struct lol : Rule::Body {
  int i;
  lol(int i): i(i) {}
  void eval_body(Rule&) override {}
  void print(std::ostream& os) const override
  {
    os << "lol(" << i << ")";
  }
  int operator[](const std::vector<int>&) const;
};

struct quux : Rule::Head {
  double f;
  quux(double f): f(f) {}
  void eval_head(Rule&) override {}
  void print(std::ostream& os) const override
  {
    os << "quux(" << f << ")";
  }
};

struct baz : public Rule::Head, public Rule::Body {
  std::string s;
  baz(const std::string &s): s(s) {}
  void eval_head(Rule&) override {}
  void eval_body(Rule&) override {}
  void print(std::ostream& os) const override
  {
    os << "baz(" << s << ")";
  }
};


template<class T>
std::vector<T> operator|(const std::vector<T> v, const T& t)
{
  std::vector<T> res(v);
  res.push_back(t);
  return res;
}

Rule::ubody mklol(int i) { return std::make_unique<lol>(i); }
Rule::uhead mkquux(double f) { return std::make_unique<quux>(f); }

Rule::ubody baz_b(const std::string& s) { return std::make_unique<baz>(s); }
Rule::uhead baz_h(const std::string& s) { return std::make_unique<baz>(s); }

#include <deque>

int main()
{
  Rule myrule = mkquux(3.5) << mklol(1) & mklol(2) & mklol(3);

  std::cout << myrule << "\n";

  auto bh = baz_h("baz_h");
  std::cout << *bh << "\n";
  auto bb = baz_b("baz_b");
  std::cout << *bb << "\n";

  // Query Q = (mkquux(3.14) << mklol(1) & mklol(2) & mklol(3),
  //            mkquux(3.78) << mklol(4) & mklol(5));

  Query Q{
    {
      mkquux(3.14) << mklol(1) & mklol(2) & mklol(3),
      mkquux(3.78) << mklol(4) & mklol(5),
    }
  };

  std::cout << "Q:" << Q << "\n";

  std::cout << sizeof(std::vector<Query>) << " " << sizeof(std::deque<Query>) << " " << sizeof(std::shared_ptr<Rule::Head>)<< "\n";
  // Query(Path(x, y) <<  E(x, y) |

  //                  <<= E(x, )
  //       Path(x, z) <<  E(x, y) & Path(y, z));
}
