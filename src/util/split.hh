// split(): break a string into pieces at every occurrence of a separator.
//
// Used in this project by: video/yuv4mpeg.cc only (tokenizing the Y4M file
// header), which itself is not used by the apps. So this is currently
// UNUSED by src/app.

#ifndef SPLIT_HH
#define SPLIT_HH

#include <string>
#include <vector>

// e.g. split("a,,b", ",") == {"a", "", "b"}. Adjacent separators give empty
// pieces; a trailing separator does not add an empty last piece, and an
// empty input gives an empty vector. Throws if 'separator' is empty.
std::vector<std::string> split(const std::string & str,
                               const std::string & separator);

#endif /* SPLIT_HH */
