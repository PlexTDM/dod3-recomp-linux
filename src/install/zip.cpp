/* .zip reading for the setup: see install/zip.h. */
#include "install/zip.h"
#include "util.h"

#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>

namespace dod3setup {

namespace fs = std::filesystem;

namespace {

uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

struct File {
    FILE* f = nullptr;
    explicit File(const fs::path& p, const char* mode) : f(dod3::open_file(p, mode)) {}
    ~File()
    {
        if (f) fclose(f);
    }
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

}  // namespace

bool zip_list(const fs::path& zip, std::vector<ZipEntry>* entries, std::string* err)
{
    entries->clear();
    std::error_code ec;
    const uint64_t size = fs::file_size(zip, ec);
    File in(zip, "rb");
    if (ec || !in.f || size < 22) {
        *err = "cannot read the archive";
        return false;
    }
    /* the end of central directory record: in the last 64 KB + 22 bytes */
    const uint64_t tail = std::min<uint64_t>(size, 65536 + 22);
    std::vector<uint8_t> t((size_t)tail);
    if (!in.seek(size - tail) || !in.read(t.data(), t.size())) {
        *err = "cannot read the archive";
        return false;
    }
    size_t eocd = SIZE_MAX;
    for (size_t i = t.size() - 22 + 1; i-- > 0;)
        if (le32(&t[i]) == 0x06054b50u) {
            eocd = i;
            break;
        }
    if (eocd == SIZE_MAX) {
        *err = "not a .zip archive";
        return false;
    }
    const uint32_t count = le16(&t[eocd + 10]), cd_size = le32(&t[eocd + 12]), cd_off = le32(&t[eocd + 16]);
    if (cd_off == 0xFFFFFFFFu || count == 0xFFFF || (uint64_t)cd_off + cd_size > size) {
        *err = "a ZIP64 archive, which the setup does not read: unpack it and add what is inside";
        return false;
    }
    std::vector<uint8_t> cd(cd_size);
    if (!in.seek(cd_off) || !in.read(cd.data(), cd.size())) {
        *err = "cannot read the archive";
        return false;
    }
    for (size_t p = 0, i = 0; i < count; i++) {
        if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50u) {
            *err = "the archive's directory is damaged";
            return false;
        }
        ZipEntry e;
        const uint16_t flags = le16(&cd[p + 8]);
        e.method = le16(&cd[p + 10]);
        e.crc = le32(&cd[p + 16]);
        e.csize = le32(&cd[p + 20]);
        e.usize = le32(&cd[p + 24]);
        const uint16_t n = le16(&cd[p + 28]), x = le16(&cd[p + 30]), c = le16(&cd[p + 32]);
        e.header = le32(&cd[p + 42]);
        if (p + 46 + n > cd.size()) {
            *err = "the archive's directory is damaged";
            return false;
        }
        e.name.assign((const char*)&cd[p + 46], n);
        e.dir = !e.name.empty() && e.name.back() == '/';
        if (flags & 1) {
            *err = "an encrypted archive";
            return false;
        }
        if (!e.dir && e.method != 0 && e.method != 8) {
            *err = "the archive uses a compression the setup does not read (" + std::to_string(e.method) + ")";
            return false;
        }
        if (e.name.find("..") != std::string::npos || e.name[0] == '/' || e.name[0] == '\\') {
            *err = "the archive names a file outside its folder";
            return false;
        }
        entries->push_back(e);
        p += 46 + (size_t)n + x + c;
    }
    return true;
}

bool zip_extract(const fs::path& zip, const ZipEntry& e, const fs::path& dest,
                 const std::function<bool(uint64_t)>& progress, std::string* err)
{
    File in(zip, "rb");
    uint8_t lh[30];
    if (!in.f || !in.seek(e.header) || !in.read(lh, sizeof lh) || le32(lh) != 0x04034b50u) {
        *err = "cannot read " + e.name + " from the archive";
        return false;
    }
    if (!in.seek(e.header + 30 + le16(&lh[26]) + le16(&lh[28]))) {
        *err = "cannot read " + e.name + " from the archive";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    File out(dest, "wb");
    if (!out.f) {
        *err = "cannot write " + dod3::utf8(dest);
        return false;
    }
    std::vector<uint8_t> ibuf(1u << 20), obuf(1u << 20);
    uLong crc = crc32(0, nullptr, 0);
    uint64_t left = e.csize, written = 0, done = 0;
    z_stream z{};
    if (e.method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        *err = "internal error: zlib";
        return false;
    }
    std::unique_ptr<z_stream, void (*)(z_stream*)> zguard(e.method == 8 ? &z : nullptr,
                                                          [](z_stream* s) { inflateEnd(s); });
    bool ended = e.method != 8;
    while (left > 0) {
        const size_t n = (size_t)std::min<uint64_t>(ibuf.size(), left);
        if (!in.read(ibuf.data(), n)) {
            *err = "the archive is cut short";
            return false;
        }
        left -= n;
        done += n;
        if (e.method == 0) {
            crc = crc32(crc, ibuf.data(), (uInt)n);
            if (fwrite(ibuf.data(), 1, n, out.f) != n) {
                *err = "disk full or not writable";
                return false;
            }
            written += n;
        } else {
            z.next_in = ibuf.data();
            z.avail_in = (uInt)n;
            while (z.avail_in > 0 && !ended) {
                z.next_out = obuf.data();
                z.avail_out = (uInt)obuf.size();
                const int r = inflate(&z, Z_NO_FLUSH);
                if (r != Z_OK && r != Z_STREAM_END) {
                    *err = e.name + " in the archive is damaged";
                    return false;
                }
                const size_t got = obuf.size() - z.avail_out;
                crc = crc32(crc, obuf.data(), (uInt)got);
                if (fwrite(obuf.data(), 1, got, out.f) != got) {
                    *err = "disk full or not writable";
                    return false;
                }
                written += got;
                ended = r == Z_STREAM_END;
            }
        }
        if (progress && !progress(done)) {
            *err = "cancelled";
            return false;
        }
    }
    if (!ended || written != e.usize || crc != e.crc) {
        *err = e.name + " in the archive is damaged (its checksum does not match)";
        return false;
    }
    return true;
}

}  // namespace dod3setup
