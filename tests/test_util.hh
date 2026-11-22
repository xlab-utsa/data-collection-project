#ifndef TEST_UTIL_HH
#define TEST_UTIL_HH
// Minimal assertion helpers so the tests need nothing beyond a C++17 compiler.
#include <cstdio>

inline int & test_failures() { static int n = 0; return n; }

#define CHECK(cond) do { if (!(cond)) { \
  std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
  test_failures()++; } } while (0)

inline int test_summary(const char * name)
{
  if (test_failures() == 0) { std::printf("%s: OK\n", name); return 0; }
  std::printf("%s: %d failure(s)\n", name, test_failures());
  return 1;
}
#endif
