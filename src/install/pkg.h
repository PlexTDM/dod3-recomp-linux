/*
 * PS3 package (.pkg) reading for the setup: the 1.01 update, the DLC, the
 * US PSN release's game package and homebrew (mod) packages.
 *
 * A retail package is a header, a file table and the files, the table and
 * files under AES-128-CTR with the retail package key (ps3recomp's, from its
 * tools/pkg_extract.py; it opens the package wrapper and nothing else --
 * licensed content inside stays as it is). Its last 32 bytes are the SHA-1 of
 * everything before them, which is also the checksum Sony's update server
 * lists, so a package checks itself: extract() hashes the file as it reads
 * it and fails on a mismatch.
 *
 * A debug package -- what homebrew tools make: fan translations come as
 * these -- is laid out the same, under a keystream of SHA-1s instead: each
 * 16 bytes are XORed with the first 16 of SHA-1(QA digest's two halves, each
 * twice, 24 zero bytes, the 16-byte block's index as a big-endian u64).
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>

namespace dod3setup {

struct PkgEntry {
    std::string name;        /* e.g. "USRDIR/PATCH/SQEX03GAME/COOKEDPS3/CORE.XXX" */
    uint64_t offset = 0;     /* in the data area */
    uint64_t size = 0;
    bool dir = false;
};

struct Pkg {
    std::filesystem::path path;
    std::string content_id;  /* "UP0082-BLUS31197_00-DOD3PATCH0000000" */
    uint64_t file_size = 0;
    uint64_t data_offset = 0, data_size = 0;
    uint8_t riv[16] = {};
    bool debug = false;      /* a debug package: the QA digest's keystream */
    uint8_t qa[16] = {};
    /* The SHA-1 extract() checks: the footer's unless set here (homebrew
     * packages may leave their footer zero; one left zero and unset is not
     * checked). */
    std::string expect_sha1;
    std::vector<PkgEntry> entries;
    uint64_t payload() const;        /* bytes of the files */
};

/* A package's first four bytes, "\x7FPKG". */
const uint32_t kPkgMagic = 0x7F504B47u;

/* Read the header and the file table. False (with `err`) if it is not a PS3
 * package (retail or debug). */
bool pkg_open(const std::filesystem::path& p, Pkg* pkg, std::string* err);

/* Just the content ID, from the plain-text header (cheap). */
std::string pkg_content_id(const std::filesystem::path& p);

/* Is it a debug package (a homebrew one), from the header alone (cheap). */
bool pkg_is_debug(const std::filesystem::path& p);

/* Extract the whole package under `dest`. Reads it once, start to end,
 * checking its SHA-1 footer. progress(bytes read, total) returns false to
 * cancel. `layout`, if given, names where each entry goes under `dest`
 * ("" leaves it out). */
bool pkg_extract(const Pkg& pkg, const std::filesystem::path& dest,
                 const std::function<bool(uint64_t, uint64_t)>& progress, std::string* err,
                 const std::function<std::string(const std::string&)>& layout = nullptr);

/* One file of the package, decrypted into memory (EBOOT.BIN). Not checked
 * against the footer: what is read from it is checked by its own hash. */
bool pkg_read_file(const Pkg& pkg, const std::string& name, std::vector<uint8_t>* out, std::string* err);

/* The footer's SHA-1 (hex): what the update server lists. */
std::string pkg_footer_sha1(const std::filesystem::path& p);

}  // namespace dod3setup
