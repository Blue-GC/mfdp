#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct TestFailure {
  std::string message;
};

#define TEST(name)                                  \
  static void test_##name();                        \
  static Registrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::ostringstream os_;                                                         \
      os_ << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond;                 \
      throw TestFailure{os_.str()};                                                   \
    }                                                                                 \
  } while (0)

#define CHECK_EQ(a, b)                                                                \
  do {                                                                                \
    auto va_ = (a);                                                                   \
    auto vb_ = (b);                                                                   \
    if (!(va_ == vb_)) {                                                              \
      std::ostringstream os_;                                                         \
      os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b        \
          << " (" << va_ << " vs " << vb_ << ")";                                     \
      throw TestFailure{os_.str()};                                                   \
    }                                                                                 \
  } while (0)

#define CHECK_THROWS_CODE(expr, errcode)                                              \
  do {                                                                                \
    bool thrown_ = false;                                                             \
    try {                                                                             \
      expr;                                                                           \
    } catch (const mfdp::Error& e_) {                                              \
      thrown_ = true;                                                                 \
      if (e_.code() != mfdp::ErrorCode::errcode) {                                 \
        std::ostringstream os_;                                                       \
        os_ << __FILE__ << ":" << __LINE__ << ": wrong error code: " << e_.describe();\
        throw TestFailure{os_.str()};                                                 \
      }                                                                               \
    }                                                                                 \
    if (!thrown_) {                                                                   \
      std::ostringstream os_;                                                         \
      os_ << __FILE__ << ":" << __LINE__ << ": expected " #errcode " from " #expr;    \
      throw TestFailure{os_.str()};                                                   \
    }                                                                                 \
  } while (0)
