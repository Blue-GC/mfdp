#include "test_framework.h"

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  int failed = 0;
  int ran = 0;
  for (auto& t : registry()) {
    if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
    ++ran;
    try {
      t.fn();
      std::cout << "[ OK ] " << t.name << "\n";
    } catch (const TestFailure& f) {
      ++failed;
      std::cout << "[FAIL] " << t.name << "\n       " << f.message << "\n";
    } catch (const std::exception& e) {
      ++failed;
      std::cout << "[FAIL] " << t.name << "\n       exception: " << e.what() << "\n";
    }
  }
  std::cout << ran - failed << "/" << ran << " tests passed\n";
  return failed == 0 ? 0 : 1;
}
