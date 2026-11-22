// Implementation of the string/number helpers declared in conversion.hh.

#include "conversion.hh"

using namespace std;

int strict_stoi(const string & str, const int base)
{
  // 'pos' receives the number of characters std::stoi consumed; if it did
  // not consume the whole string, there was garbage after the number
  size_t pos;
  int ret = stoi(str, &pos, base);

  if (pos != str.size()) {
    throw runtime_error("strict_stoi");
  }

  return ret;
}

long long strict_stoll(const string & str, const int base)
{
  size_t pos;
  // Note: the result is stored in an int, so values outside the int range
  // are truncated even though the return type is long long. Nothing in the
  // apps calls strict_stoll today; keep this in mind before using it.
  int ret = stoll(str, &pos, base);

  if (pos != str.size()) {
    throw runtime_error("strict_stoll");
  }

  return ret;
}

string double_to_string(const double input, const int precision)
{
  stringstream stream;
  stream << fixed << setprecision(precision) << input;
  return stream.str();
}
