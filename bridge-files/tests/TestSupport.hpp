#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace FileBridgeTests
{
inline int failures = 0;

inline void report(bool ok, const char* expression, const char* file, int line)
{
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expression);
}

inline int finish(const char* suite)
{
    if (failures != 0)
    {
        std::fprintf(stderr, "%s: %d check(s) failed\n", suite, failures);
        return EXIT_FAILURE;
    }
    std::printf("%s: all checks passed\n", suite);
    return EXIT_SUCCESS;
}
}

#define CHECK(expression) ::FileBridgeTests::report(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
