/*
 * Reading a .zip for the setup: a mod's movies come as one (the Spanish
 * translation's MOVIES.zip). The central directory, then each entry stored
 * or deflated (zlib); no ZIP64, no encryption -- an archive that needs them is
 * refused. Every entry is checked against its CRC-32 as it is written.
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dod3setup {

struct ZipEntry {
    std::string name;        /* as stored: '/' separated */
    uint64_t header = 0;     /* the local header's offset */
    uint64_t csize = 0, usize = 0;
    uint32_t crc = 0;
    uint16_t method = 0;     /* 0 stored, 8 deflated */
    bool dir = false;
};

/* The archive's entries. False (with `err`) if it is not a zip this reads. */
bool zip_list(const std::filesystem::path& zip, std::vector<ZipEntry>* entries, std::string* err);

/* One entry, written to `dest` (its folders made). progress(bytes of the
 * archive read) returns false to cancel. */
bool zip_extract(const std::filesystem::path& zip, const ZipEntry& e, const std::filesystem::path& dest,
                 const std::function<bool(uint64_t)>& progress, std::string* err);

}  // namespace dod3setup
