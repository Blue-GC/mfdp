#include <string>

namespace shapes {

class Shape {
 public:
  virtual ~Shape() {}
  virtual double area() const = 0;
  std::string name() const { return label_; }

 protected:
  std::string label_;
};

class Circle : public Shape {
 public:
  explicit Circle(double r) : radius_(r) {}
  double area() const override { return 3.14159 * radius_ * radius_; }

 private:
  double radius_;
};

double total(const Circle& a, const Circle& b) { return a.area() + b.area(); }

}
