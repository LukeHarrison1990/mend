// Mend patch: a plain file instead of a symlink to pugl.cpp, so the
// repository checks out on Windows. macOS compiles it as Objective-C++.
#include "pugl.cpp"
