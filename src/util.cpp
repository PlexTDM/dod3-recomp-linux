/* Small helpers shared by the port's sources: see util.h. */
#include "util.h"

#include <stdlib.h>
#include <time.h>


extern "C" uint64_t dod3_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000u;
}

extern "C" int dod3_setenv(const char* name, const char* value, int overwrite)
{
    return setenv(name, value, overwrite);
}

extern "C" int dod3_unsetenv(const char* name)
{
    return unsetenv(name);
}

FILE* dod3::open_file(const std::filesystem::path& p, const char* mode)
{
    return fopen(p.c_str(), mode);
}
