/* A FILE* closed when it goes, with 64-bit seeks: what the setup's readers
 * of large archives use (src/install/pkg.cpp, src/install/zip.cpp). */
#pragma once
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace dod3setup {

struct File {
    FILE* f = nullptr;
    File(const std::filesystem::path& p, const char* mode) : f(dod3::open_file(p, mode)) {}
    ~File()
    {
        if (f) fclose(f);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool seek(uint64_t o)
    {
#ifdef _WIN32
        return _fseeki64(f, (long long)o, SEEK_SET) == 0;
#else
        return fseeko(f, (off_t)o, SEEK_SET) == 0;
#endif
    }
    bool read(void* p, size_t n) { return fread(p, 1, n, f) == n; }
};

}  // namespace dod3setup
