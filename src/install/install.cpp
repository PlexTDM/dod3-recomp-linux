/* The installer's core: see install/install.h. */
#include "install/install.h"
#include "install/iso.h"
#include "install/pkg.h"
#include "install/crypto.h"
#include "install/zip.h"
#include "cpu/eboot.h"
#include "util.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <chrono>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#endif

namespace dod3setup {

namespace {

/* The 1.01 update package: its content ID, and the SHA-1 its last 32 bytes
 * carry (= the checksum Sony's update server lists for it). */
const char kUpdateId[] = "UP0082-BLUS31197_00-DOD3PATCH0000000";
const char kUpdateSha1[] = "d71bc929d53703bf6899cb18d6041bcc7ecb5419";
const char kDlcPrefix[] = "UP0082-NPUB31251_00-";
/* The US PSN release (NPUB31251): its game package (1.00) and its own 1.01
 * update, by the SHA-1 their last 32 bytes carry. The game's files are the
 * disc's but for an executable of its own and COALESCED_INT.BIN, both under
 * the buyer's licence; the disc release's update brings both. Its own update
 * is the disc release's 1.01 files with another such executable, so it is
 * not taken. */
const char kPsnGameId[] = "UP0082-NPUB31251_00-DOD3MAINGAME0000";
const char kPsnGameSha1[] = "e93a19fae13a5f5d332a311d91d939fcf4987514";
const char kPsnUpdateSha1[] = "4a0ad3c8fd7abe8937da712aec8e4df519fe6250";
/* The European PSN release (NPEB01407): a build of its own, not supported. */
const char kEuPrefix[] = "EP0082-NPEB01407_00-";
/* SHA-256 of the disc's PS3TOC.TXT, which the PSN package's becomes. */
const char kDiscTocSha256[] = "7b33d9c816018889ebfb4e1caf6aa6daa51ab5d51df05d358d2ca98938e1d6c5";
/* SHA-256 of the update's EBOOT.BIN as an ELF (1.01); the disc's (1.00) is
 * install/iso.h's kEbootElfSha256. */
const char kElf101Sha256[] = "ed89e80f2336a948074c32151d2cadfb6965d174a15d9c067aeeff97c21e7e6c";

using dod3::utf8;

fs::path update_dir(const fs::path& base) { return base / "game/disc/game/BLES00000"; }
fs::path dlc_dir(const fs::path& base) { return base / "game/disc/game/NPUB31251"; }
fs::path mods_dir(const fs::path& base) { return base / "game/mods"; }
/* each mod's file list and the update's files it replaced (install.h) */
fs::path modfiles_dir(const fs::path& base) { return base / "game/modfiles"; }
fs::path staging(const fs::path& base) { return base / "game/setup.partial"; }

/* a file's first four bytes, big-endian, and the next two in `rev` */
uint32_t magic(const fs::path& p, uint16_t* rev = nullptr)
{
    FILE* f = dod3::open_file(p, "rb");
    uint8_t m[6] = {};
    if (f) {
        if (fread(m, 1, 6, f) != 6) m[0] = 0;
        fclose(f);
    }
    if (rev) *rev = (uint16_t)(m[4] << 8 | m[5]);
    return (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
}

/* a package's revision: 0x8000 is retail (PSN), 0 debug */
bool retail(const fs::path& p)
{
    uint16_t rev = 0;
    magic(p, &rev);
    return (rev & 0x8000) != 0;
}

/* fs::rename, tried again for a few seconds while it is refused: right after
 * thousands of files are written, a scanner (Windows Defender, the search
 * indexer) may still hold one of them open, and the folder cannot move until
 * it lets go ("Access is denied"). */
void move_patiently(const fs::path& from, const fs::path& to, std::error_code& ec)
{
    for (int i = 0;; i++) {
        fs::rename(from, to, ec);
        if (!ec || i == 20) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

/* Move everything under `from` into `to`, file by file (same volume: each a
 * rename), replacing what is there; then remove `from`. */
bool merge_move(const fs::path& from, const fs::path& to, std::string* err)
{
    std::error_code ec;
    fs::create_directories(to, ec);
    for (fs::directory_iterator it(from, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::path t = to / it->path().filename();
        if (it->is_directory(ec)) {
            if (!merge_move(it->path(), t, err)) return false;
            continue;
        }
        if (fs::is_directory(t, ec)) fs::remove_all(t, ec);
        move_patiently(it->path(), t, ec);
        if (ec) {
            *err = "could not move " + utf8(it->path().filename()) + " into place: " + ec.message();
            return false;
        }
    }
    if (ec) {
        *err = "could not read " + utf8(from) + ": " + ec.message();
        return false;
    }
    fs::remove(from, ec);
    return true;
}

/* ---- links onto the disc (the game-data layout) -------------------------- */

/* Is `p` a link (a junction or a symlink), whatever it points at? */
bool is_link(const fs::path& p)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    std::error_code ec;
    return fs::is_symlink(fs::symlink_status(p, ec));
#endif
}

/* Remove the link itself, never what it points at. */
bool remove_link(const fs::path& p)
{
#ifdef _WIN32
    return RemoveDirectoryW(p.c_str()) != 0;
#else
    std::error_code ec;
    return fs::remove(p, ec);
#endif
}

/* A directory link at `link` onto `target`. On Windows a junction (unlike a
 * symlink it needs no privilege; it holds an absolute path, so a moved folder
 * is repaired at the next boot), elsewhere a relative symlink. */
bool make_link(const fs::path& target, const fs::path& link)
{
    std::error_code ec;
#ifdef _WIN32
    const std::wstring abs = fs::absolute(target, ec).wstring();
    if (abs.size() < 3 || abs[1] != L':') return false;   /* a junction needs a drive path */
    const std::wstring sub = L"\\??\\" + abs;
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(link.c_str());
        return false;
    }
    /* REPARSE_DATA_BUFFER, mount-point form (ntifs.h): tag, data length,
     * reserved; substitute name offset/length, print name offset/length
     * (bytes); the two names, each NUL-terminated */
    const size_t subb = sub.size() * 2, absb = abs.size() * 2;
    std::vector<uint8_t> buf(16 + subb + 2 + absb + 2);
    auto put16 = [&](size_t o, size_t v) {
        buf[o] = (uint8_t)v;
        buf[o + 1] = (uint8_t)(v >> 8);
    };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    memcpy(&buf[0], &tag, 4);
    put16(4, buf.size() - 8);
    put16(8, 0);
    put16(10, subb);
    put16(12, subb + 2);
    put16(14, absb);
    memcpy(&buf[16], sub.c_str(), subb);
    memcpy(&buf[16 + subb + 2], abs.c_str(), absb);
    DWORD got = 0;
    const BOOL ok =
        DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), (DWORD)buf.size(), nullptr, 0, &got, nullptr);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(link.c_str());
    return ok != 0;
#else
    fs::create_directory_symlink(fs::relative(target, link.parent_path(), ec), link, ec);
    return !ec;
#endif
}

size_t count_files(const fs::path& d)
{
    std::error_code ec;
    size_t n = 0;
    for (fs::recursive_directory_iterator it(d, ec), end; it != end && !ec; it.increment(ec))
        if (it->is_regular_file(ec)) n++;
    return n;
}

/* ---- replacing the disc ----------------------------------------------- */

/* What is the disc's in game/disc; everything else there (game/: the update,
 * the DLC, the game data and its links onto the disc; cache/) is kept. */
bool disc_entry(const fs::path& name)
{
    const std::string n = utf8(name);
    return n == "PS3_GAME" || n == "PS3_DISC.SFB" || n == "PS3_UPDATE";
}

/* A disc swap moves the old game/disc to staging/old, the new copy in, then
 * the old copy's own entries back. Finish one that was cut short, or put back
 * what a failed one moved. False if something could not be put back; it then
 * stays in staging/old, and nothing there is deleted. */
bool settle_old(const fs::path& base, std::string* err)
{
    std::error_code ec;
    const fs::path old = staging(base) / "old", final_dir = base / "game/disc";
    if (!fs::exists(old, ec)) return true;
    if (!fs::exists(final_dir, ec)) {
        move_patiently(old, final_dir, ec);
        if (ec) {
            *err = "could not put the previous copy back from " + utf8(old) + ": " + ec.message();
            return false;
        }
        return true;
    }
    std::vector<fs::path> keep;
    for (fs::directory_iterator it(old, ec), end; it != end && !ec; it.increment(ec))
        if (!disc_entry(it->path().filename())) keep.push_back(it->path().filename());
    for (const fs::path& n : keep) {
        if (fs::exists(final_dir / n, ec)) {
            *err = utf8(n) + " is both in the new copy and in " + utf8(old);
            return false;
        }
        move_patiently(old / n, final_dir / n, ec);
        if (ec) {
            *err = "could not move " + utf8(n) + " back from " + utf8(old) + ": " + ec.message();
            return false;
        }
    }
    fs::remove_all(old, ec);   /* only the old disc's own files are left */
    return true;
}

/* A DLC package that holds only licence files (.EDAT): NoPayStation's "FIX"
 * packages, which swap the packs' licences for ones a free RAP unlocks. The
 * port reads the DLC without licences, so they are not wanted. */
bool licence_only(const Pkg& pkg)
{
    for (const PkgEntry& e : pkg.entries) {
        if (e.dir || e.name.rfind("USRDIR/", 0) != 0) continue;
        const size_t n = e.name.size();
        if (n < 5 || strcmp(e.name.c_str() + n - 5, ".EDAT") != 0) return false;
    }
    return true;
}

/* ---- mods -------------------------------------------------------------------
 * The ones known by name: the Spanish translation by Traducciones Vacuno
 * (TraduSquare, v1.1, for BLUS31197 1.01), as its packages and its movie
 * archive, and the DLC chapters' Japanese voices (the DLC Undub by Canzah and
 * Slow). The packages are homebrew ones, most with no checksum in their
 * footer: each is told by its size and QA digest and checked against its
 * SHA-1 as it is installed. */
struct KnownPkg {
    const char* id;
    uint64_t size;
    const char* qa;      /* the header's QA digest */
    const char* sha1;    /* of all but the last 32 bytes, as extract() hashes it */
};
const KnownPkg kKnownPkgs[] = {
    { "ES-GAME", 537394384, "12034f397b199249954df576786cc78f", "99823fc1dcdfb442d16e07f38eeed48966da0910" },
    { "ES-DATA", 1525988592, "0fbad69750dfe5c7c559a30cfbc89a61", "8ed55ecea67c554189294c5f646b95b11ec77df1" },
    { "ES-DLC", 291400080, "e5f6b1e037ae01edf7ce89e307a02b2e", "5561c8e5e8c21ee041e7bbf81eacdf6146a69dcf" },
    { "ES-JPV", 445111920, "d1b828cfd2c518489b631840cdbbe2df", "4e30202af512b485209cc2034ae7c3ff93461e40" },
};
/* MOVIES.zip: its five movies, by name and CRC-32 */
const struct {
    const char* name;
    uint32_t crc;
} kEsMovies[] = {
    { "D3_OPN_TEST.BIK", 0xde105c0au }, { "M1040_E0900.BIK", 0x2f3f7de9u }, { "M2040_E0070.BIK", 0x9d2985a8u },
    { "M5320_E0010.BIK", 0xc6c15805u }, { "STARTLOAD.BIK", 0x5f67cbc8u },
};

std::string upper(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::string hex(const uint8_t* p, size_t n)
{
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

const KnownPkg* known_pkg(const Pkg& pkg)
{
    for (const KnownPkg& k : kKnownPkgs)
        if (pkg.file_size == k.size && hex(pkg.qa, 16) == k.qa) return &k;
    return nullptr;
}

std::string mod_name(const std::string& id)
{
    for (const DlcInfo& m : known_mods())
        if (id == m.id) return m.name;
    return id;
}

/* Where a mod package's file goes, by its path in the package and the title
 * it is for: "patch/..." into the update's PATCH folder (as the console has
 * it), "tree/..." into game/mods at the path under game/disc it stands in for
 * (the game-data install's files, the DLC's), "" left out (the package's own
 * PARAM.SFO, icons, package.conf), "?" nowhere it could go. */
std::string mod_layout(const std::string& title, const std::string& name)
{
    if (name.find('/') == std::string::npos) return "";
    auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
    if (title == "BLUS31197") {
        if (starts("USRDIR/PATCH/")) return "patch/" + name.substr(13);
        if (starts("USRDIR/FIOS-UNREALENGINE3/")) return "tree/game/BLES00000DATA/" + name;
    } else if (title == "NPUB31251") {
        if (starts("USRDIR/DLC/") || starts("USRDIR/DLC_JPV/")) return "tree/game/NPUB31251/" + name;
    }
    return "?";
}

/* A movie archive's entries: every file a .BIK, in MOVIES/ or at the top. */
bool movie_entries(const std::vector<ZipEntry>& es)
{
    size_t n = 0;
    for (const ZipEntry& e : es) {
        if (e.dir) continue;
        const std::string u = upper(e.name);
        const size_t sl = u.find('/');
        if (u.size() < 5 || u.compare(u.size() - 4, 4, ".BIK") != 0) return false;
        if (sl != std::string::npos && u.rfind("MOVIES/", 0) != 0) return false;
        if (u.find('/', sl == std::string::npos ? 0 : sl + 1) != std::string::npos) return false;
        n++;
    }
    return n > 0;
}

/* The DLC Undub's folder: the one holding GAME (or GAME itself), its
 * GAME/NPUB31251/USRDIR/DLC holding ADDSCENARIO folders. "" if not. */
fs::path undub_dlc_dir(const fs::path& p)
{
    std::error_code ec;
    for (const fs::path& g : { p / "GAME", p }) {
        const fs::path d = g / "NPUB31251/USRDIR/DLC";
        for (int i = 0; i < 6; i++)
            if (fs::is_directory(d / ("ADDSCENARIO0000" + std::to_string(i)), ec)) return d;
    }
    return {};
}

/* The release the DLC Undub's readme names in its first line ("Drakengard 3
 * DLC Undub Prototype ver.... by ..."), else "". */
std::string undub_release(const fs::path& folder)
{
    std::string l;
    if (FILE* f = dod3::open_file(folder / "Readme.txt", "rb")) {
        char line[256] = "";
        if (fgets(line, sizeof line, f)) l = line;
        fclose(f);
    }
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
    return l.find("Undub") != std::string::npos && l.size() < 160 ? l : std::string();
}

/* game/mods/installed.txt: one line a mod, "<id>\t<where>\t<name>"; where is
 * "patch" (the update's folder), "tree" (game/mods), "jpv" (the voice pack's
 * folder), or a comma-separated mix. */
std::vector<std::vector<std::string>> mods_read(const fs::path& base)
{
    std::vector<std::vector<std::string>> out;
    FILE* f = dod3::open_file(mods_dir(base) / "installed.txt", "rb");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        std::vector<std::string> cols;
        size_t at = 0;
        for (int i = 0; i < 2; i++) {
            const size_t t = l.find('\t', at);
            if (t == std::string::npos) break;
            cols.push_back(l.substr(at, t - at));
            at = t + 1;
        }
        if (cols.size() == 2) {
            cols.push_back(l.substr(at));
            out.push_back(cols);
        }
    }
    fclose(f);
    return out;
}

bool mods_write(const fs::path& base, const std::vector<std::vector<std::string>>& rows)
{
    std::error_code ec;
    fs::create_directories(mods_dir(base), ec);
    FILE* f = dod3::open_file(mods_dir(base) / "installed.txt", "wb");
    if (!f) return false;
    for (const auto& r : rows) fprintf(f, "%s\t%s\t%s\n", r[0].c_str(), r[1].c_str(), r[2].c_str());
    fclose(f);
    return true;
}

void mods_record(const fs::path& base, const std::string& id, const std::string& where, const std::string& name)
{
    auto rows = mods_read(base);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& r) { return r[0] == id; }), rows.end());
    rows.push_back({ id, where, name });
    mods_write(base, rows);
}

/* Where the PSN game package's files go: under PS3_GAME, as on the disc. Its
 * EBOOT.BIN and its one .EDAT (COALESCED_INT.BIN, under licence) are left
 * out; the update brings the executable and the plain file. */
std::string psn_layout(const std::string& name)
{
    const size_t n = name.size();
    if (name == "USRDIR/EBOOT.BIN" || (n > 5 && name.compare(n - 5, 5, ".EDAT") == 0)) return "";
    return "PS3_GAME/" + name;
}

/* The PSN package's file list names that file as its .EDAT, where the disc's
 * names the plain file; with the line as on the disc, the list is the disc's
 * own (checked). */
bool psn_fix_toc(const fs::path& disc, std::string* err)
{
    const fs::path toc = disc / "PS3_GAME/USRDIR/SQEX03GAME/PS3TOC.TXT";
    std::error_code ec;
    const uint64_t n = fs::file_size(toc, ec);
    std::string s(ec ? 0 : (size_t)n, '\0');
    FILE* f = ec ? nullptr : dod3::open_file(toc, "rb");
    const bool read = f && fread(&s[0], 1, s.size(), f) == s.size();
    if (f) fclose(f);
    const std::string from = "\\CookedPS3\\COALESCED_INT.BIN.EDAT ", to = "\\CookedPS3\\Coalesced_INT.bin ";
    const size_t at = s.find(from);
    if (read && at != std::string::npos) s.replace(at, from.size(), to);
    if (!read || sha256_hex(s.data(), s.size()) != kDiscTocSha256) {
        *err = "the PSN package's file list (PS3TOC.TXT) is not the one expected";
        return false;
    }
    f = dod3::open_file(toc, "wb");
    const bool wrote = f && fwrite(s.data(), 1, s.size(), f) == s.size();
    if (f) fclose(f);
    if (!wrote) *err = "could not write " + utf8(toc);
    return wrote;
}

/* The DLC Undub: the six chapters' voice, lip-sync and message packages and
 * their movies with Japanese voices (the readme's GAME/NPUB31251), put
 * beside the voice pack's own in DLC_JPV/UNDUB and added to its file lists
 * (dlc_write_jpv_lists), so the voice option switches them with the rest:
 * Japanese, they are read in place of the chapter's own; English, the
 * chapter's own are. The title finds a voice-pack file by its name alone, so
 * a name the undub has in more than one chapter, or one the main game has
 * too, goes in a folder of its chapter's (UNDUB/ADDSCENARIO0000n) and is
 * served by chapter (src/kernel/mods.cpp); the others in UNDUB/COOKEDPS3, the
 * movies in UNDUB/MOVIES. The readme's GAME/BLUS31197DATA and ISO hold the
 * 1.01 update's ten message packages unchanged (for a PS3 running 1.00):
 * they are not needed. */
bool install_undub(const fs::path& base, const Source& s, const fs::path& stage,
                   const std::function<bool(uint64_t)>& progress, std::string* err)
{
    std::error_code ec;
    const fs::path jpv = dlc_dir(base) / "USRDIR/DLC_JPV";
    if (!fs::is_directory(jpv / "SQEX03GAME", ec)) {
        *err = "the DLC chapters' Japanese voices need the Japanese Voice Pack: add its package too";
        return false;
    }
    const fs::path src = undub_dlc_dir(s.path);
    struct Item {
        fs::path from;
        int chapter;
        bool movie;
    };
    std::vector<Item> items;
    std::map<std::string, int> chapters_of;   /* a package name: in how many chapters */
    for (int i = 0; i < 6; i++) {
        const fs::path d = src / ("ADDSCENARIO0000" + std::to_string(i));
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const bool movie = upper(it->path().parent_path().filename().string()) == "MOVIES";
            items.push_back({ it->path(), i, movie });
            if (!movie) chapters_of[upper(it->path().filename().string())]++;
        }
    }
    if (items.empty()) {
        *err = "no DLC chapter files in " + utf8(src);
        return false;
    }
    const fs::path cooked = base / "game/disc/PS3_GAME/USRDIR/SQEX03GAME/COOKEDPS3";
    auto shared = [&](const fs::path& f) {
        const std::string n = f.filename().string();
        return chapters_of[upper(n)] > 1 || fs::exists(cooked / n, ec) || fs::exists(jpv / "SQEX03GAME/COOKEDPS3" / n, ec);
    };
    const fs::path part = stage / "UNDUB";
    fs::remove_all(part, ec);
    std::set<std::string> by_chapter;
    uint64_t done = 0;
    for (const Item& it : items) {
        const std::string n = it.from.filename().string();
        fs::path to;
        if (it.movie)
            to = part / "MOVIES" / n;
        else if (shared(it.from)) {
            to = part / ("ADDSCENARIO0000" + std::to_string(it.chapter)) / n;
            by_chapter.insert(upper(n));
        } else
            to = part / "COOKEDPS3" / n;
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(it.from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            *err = "could not copy " + utf8(it.from) + ": " + ec.message();
            return false;
        }
        done += fs::file_size(to, ec);
        if (!progress(done)) {
            *err = "cancelled";
            return false;
        }
    }
    /* UNDUB.TXT: the release, then the names served by chapter */
    const std::string release = undub_release(s.path);
    std::string txt = (release.empty() ? s.name : release) + "\r\n";
    for (const std::string& n : by_chapter) txt += "C " + n + "\r\n";
    if (FILE* f = dod3::open_file(part / "UNDUB.TXT", "wb")) {
        fwrite(txt.data(), 1, txt.size(), f);
        fclose(f);
    }
    fs::remove_all(jpv / "UNDUB", ec);   /* a real folder (no links): the undub installed before */
    move_patiently(part, jpv / "UNDUB", ec);
    if (ec) {
        *err = "could not move the DLC chapters' Japanese voices into place: " + ec.message();
        return false;
    }
    if (!dlc_write_jpv_lists(base / "game/disc", true)) {
        *err = "the DLC chapters' Japanese voices installed, but the voice pack's file lists could not be written";
        return false;
    }
    mods_record(base, s.id, "jpv", s.name);
    return true;
}

/* A mod's list of its files (game/modfiles/<id>.txt): "patch/<path>" in the
 * update's PATCH folder, "tree/<path>" in game/mods, with "\t1" after it when
 * the update's own file there was kept aside (game/modfiles/<id>/orig/<path>).
 * Switched off (set_mod_enabled), the mod's files are in
 * game/modfiles/<id>/off/<kind>/<path> and <id>.off says so. */
struct ModFile {
    std::string kind;
    fs::path rel;
    bool kept = false;
};

bool mod_files(const fs::path& base, const std::string& id, std::vector<ModFile>* out)
{
    FILE* f = dod3::open_file(modfiles_dir(base) / (id + ".txt"), "rb");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        ModFile m;
        m.kept = l.size() > 2 && l.compare(l.size() - 2, 2, "\t1") == 0;
        if (m.kept) l.resize(l.size() - 2);
        const size_t sl = l.find('/');
        if (sl == std::string::npos || l.find("..") != std::string::npos) continue;
        m.kind = l.substr(0, sl);
        m.rel = fs::u8path(l.substr(sl + 1));
        if ((m.kind != "patch" && m.kind != "tree") || m.rel.empty() || m.rel.is_absolute()) continue;
        out->push_back(m);
    }
    fclose(f);
    return true;
}

fs::path mod_root(const fs::path& base, const std::string& kind)
{
    return kind == "patch" ? update_dir(base) / "USRDIR/PATCH" : mods_dir(base);
}

/* the folders a mod's files were in, removed where it leaves them empty (the deepest first) */
void remove_empty_dirs(const std::set<fs::path>& dirs)
{
    std::error_code ec;
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it)
        if (fs::is_directory(*it, ec) && fs::is_empty(*it, ec)) fs::remove(*it, ec);
}

/* One mod removed (Plan::remove_ids): the DLC Undub's folder, the voice
 * pack's lists written again; any other by its list -- its files in
 * game/mods and the update's folder deleted (or, switched off, the ones set
 * aside), the update's own put back, the folders it leaves empty removed. A
 * mod without a list (mod_removable) is left as it is, with an error. */
bool remove_mod(const fs::path& base, const std::string& id, std::string* err)
{
    std::error_code ec;
    auto drop_row = [&] {
        auto rows = mods_read(base);
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& r) { return r[0] == id; }), rows.end());
        mods_write(base, rows);
    };
    if (id == "UNDUB") {
        const fs::path jpv = dlc_dir(base) / "USRDIR/DLC_JPV";
        fs::remove_all(jpv / "UNDUB", ec);
        if (ec) {
            *err = "could not remove " + utf8(jpv / "UNDUB") + ": " + ec.message();
            return false;
        }
        if (fs::is_directory(jpv / "SQEX03GAME", ec) && !dlc_write_jpv_lists(base / "game/disc", true)) {
            *err = "could not write the voice pack's file lists again";
            return false;
        }
        drop_row();
        return true;
    }
    const fs::path listf = modfiles_dir(base) / (id + ".txt"), orig = modfiles_dir(base) / id / "orig",
                   off = modfiles_dir(base) / id / "off";
    std::vector<ModFile> list;
    if (!mod_files(base, id, &list)) {
        *err = "the files of " + id + " were not listed when it was installed: add the update package and remove "
               "all the mods";
        return false;
    }
    std::set<fs::path> dirs;
    for (const ModFile& m : list) {
        const fs::path root = mod_root(base, m.kind), live = root / m.rel;
        if (fs::exists(off / m.kind / m.rel, ec)) {   /* switched off: the update's own is back already */
            if (m.kept && !fs::exists(live, ec)) move_patiently(orig / m.rel, live, ec);
        } else {
            fs::remove(live, ec);
            if (m.kept) move_patiently(orig / m.rel, live, ec);
        }
        ec.clear();
        for (fs::path d = m.rel.parent_path(); !d.empty(); d = d.parent_path()) dirs.insert(root / d);
    }
    remove_empty_dirs(dirs);
    fs::remove_all(modfiles_dir(base) / id, ec);
    fs::remove(listf, ec);
    fs::remove(modfiles_dir(base) / (id + ".off"), ec);
    drop_row();
    return true;
}

/* A mod: a package's files where mod_layout puts them, a movie archive's
 * movies into game/mods (in place of the disc's), the DLC Undub (above). */
bool install_mod(const fs::path& base, const Source& s, const fs::path& stage,
                 const std::function<bool(uint64_t)>& progress, std::string* err)
{
    std::error_code ec;
    if (s.form == ModForm::Undub) return install_undub(base, s, stage, progress, err);
    const fs::path part = stage / "mod";
    fs::remove_all(part, ec);
    if (s.form == ModForm::Zip) {
        std::vector<ZipEntry> es;
        if (!zip_list(s.path, &es, err)) return false;
        uint64_t before = 0;
        for (const ZipEntry& e : es) {
            if (e.dir) continue;
            const std::string n = fs::path(e.name).filename().string();
            if (!zip_extract(s.path, e, part / "tree/PS3_GAME/USRDIR/SQEX03GAME/MOVIES" / n,
                             [&](uint64_t x) { return progress(before + x); }, err))
                return false;
            before += e.csize;
        }
    } else {
        Pkg pkg;
        if (!pkg_open(s.path, &pkg, err)) return false;
        if (const KnownPkg* k = known_pkg(pkg)) pkg.expect_sha1 = k->sha1;
        const std::string title = pkg.content_id.size() >= 16 ? pkg.content_id.substr(7, 9) : "";
        if (!pkg_extract(pkg, part, [&](uint64_t x, uint64_t) { return progress(x); }, err,
                         [&](const std::string& n) {
                             const std::string to = mod_layout(title, n);
                             return to == "?" ? std::string() : to;
                         }))
            return false;
    }
    /* The same mod installed before: its files go first (the update's own
     * put back), so what it replaces now is the game's. One installed before
     * the lists were kept, with files in the update's folder, is installed
     * over without a list: removing it still takes the update. */
    bool listed = true;
    for (const auto& r : mods_read(base))
        if (r[0] == s.id && !mod_removable(base, s.id)) listed = r[1].find("patch") == std::string::npos;
    if (listed && mod_removable(base, s.id) && !remove_mod(base, s.id, err)) return false;
    std::string where, list;
    auto add_files = [&](const fs::path& root, const char* kind, const fs::path& to, const fs::path& orig) {
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path rel = fs::relative(it->path(), root, ec);
            bool kept = false;
            if (!orig.empty() && fs::exists(to / rel, ec)) {   /* the update's own: kept aside */
                fs::create_directories((orig / rel).parent_path(), ec);
                move_patiently(to / rel, orig / rel, ec);
                kept = !ec;
                ec.clear();
            }
            const auto g = rel.generic_u8string();
            list += std::string(kind) + "/" + std::string(g.begin(), g.end()) + (kept ? "\t1\n" : "\n");
        }
    };
    if (fs::exists(part / "patch", ec)) {
        if (!installed(base).update) {
            *err = s.name + " goes into the 1.01 update's folder: add the update package too";
            return false;
        }
        const fs::path patch = update_dir(base) / "USRDIR/PATCH";
        if (listed) add_files(part / "patch", "patch", patch, modfiles_dir(base) / s.id / "orig");
        if (!merge_move(part / "patch", patch, err)) return false;
        where = "patch";
    }
    if (fs::exists(part / "tree", ec)) {
        if (listed) add_files(part / "tree", "tree", mods_dir(base), fs::path());
        if (!merge_move(part / "tree", mods_dir(base), err)) return false;
        where += where.empty() ? "tree" : ",tree";
    }
    fs::remove_all(part, ec);
    if (listed) {
        fs::create_directories(modfiles_dir(base), ec);
        if (FILE* f = dod3::open_file(modfiles_dir(base) / (s.id + ".txt"), "wb")) {
            fwrite(list.data(), 1, list.size(), f);
            fclose(f);
        }
    }
    mods_record(base, s.id, where, s.name);
    return true;
}

/* The installed mods removed (Plan::remove_mods): game/mods' files (the
 * setup's own, no links there) and the DLC chapters' voices, the voice
 * pack's lists written again without them. A mod with files in the update's
 * folder stays listed until the update is installed again. */
bool remove_mods(const fs::path& base, std::string* err)
{
    std::error_code ec;
    for (const auto& r : mods_read(base))   /* the listed ones: the update's own files put back */
        if (r[0] != "UNDUB" && mod_removable(base, r[0]) && !remove_mod(base, r[0], err)) return false;
    for (fs::directory_iterator it(mods_dir(base), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename() != "installed.txt") fs::remove_all(it->path(), ec);
    const fs::path jpv = dlc_dir(base) / "USRDIR/DLC_JPV";
    if (fs::exists(jpv / "UNDUB", ec)) {
        fs::remove_all(jpv / "UNDUB", ec);
        if (ec) {
            *err = "could not remove " + utf8(jpv / "UNDUB") + ": " + ec.message();
            return false;
        }
        if (fs::is_directory(jpv / "SQEX03GAME", ec) && !dlc_write_jpv_lists(base / "game/disc", true)) {
            *err = "could not write the voice pack's file lists again";
            return false;
        }
    }
    auto rows = mods_read(base);
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const auto& r) { return r[1].find("patch") == std::string::npos; }),
               rows.end());
    for (auto& r : rows) r[1] = "patch";
    mods_write(base, rows);
    return true;
}

}  // namespace

bool mods_need_update(const fs::path& base)
{
    for (const auto& r : mods_read(base))
        if (r[1].find("patch") != std::string::npos && !mod_removable(base, r[0])) return true;
    return false;
}

bool mod_removable(const fs::path& base, const std::string& id)
{
    std::error_code ec;
    return id == "UNDUB" || fs::exists(modfiles_dir(base) / (id + ".txt"), ec);
}

bool mod_switchable(const fs::path& base, const std::string& id)
{
    std::error_code ec;
    return id != "UNDUB" && fs::exists(modfiles_dir(base) / (id + ".txt"), ec);
}

bool mod_enabled(const fs::path& base, const std::string& id)
{
    std::error_code ec;
    return !fs::exists(modfiles_dir(base) / (id + ".off"), ec);
}

/* File by file, each step done only if it is still to do, so a switch cut
 * short is finished by doing it again. */
bool set_mod_enabled(const fs::path& base, const std::string& id, bool on, std::string* err)
{
    std::error_code ec;
    std::vector<ModFile> list;
    if (!mod_switchable(base, id) || !mod_files(base, id, &list)) {
        *err = "this mod was installed without a list of its files; install it again to switch it on and off";
        return false;
    }
    const fs::path orig = modfiles_dir(base) / id / "orig", off = modfiles_dir(base) / id / "off",
                   mark = modfiles_dir(base) / (id + ".off");
    auto move = [&](const fs::path& from, const fs::path& to) {
        fs::create_directories(to.parent_path(), ec);
        ec.clear();
        move_patiently(from, to, ec);
        if (!ec) return true;
        *err = "could not move " + utf8(from) + " to " + utf8(to) + ": " + ec.message();
        return false;
    };
    std::set<fs::path> dirs;
    for (const ModFile& m : list) {
        const fs::path root = mod_root(base, m.kind), live = root / m.rel, aside = off / m.kind / m.rel,
                       own = orig / m.rel;
        if (!on) {
            if (fs::exists(live, ec) && !fs::exists(aside, ec) && !move(live, aside)) return false;
            if (m.kept && !fs::exists(live, ec) && fs::exists(own, ec) && !move(own, live)) return false;
            for (fs::path d = m.rel.parent_path(); !d.empty(); d = d.parent_path()) dirs.insert(root / d);
        } else if (fs::exists(aside, ec)) {
            if (m.kept && fs::exists(live, ec) && !fs::exists(own, ec) && !move(live, own)) return false;
            if (!fs::exists(live, ec) && !move(aside, live)) return false;
        }
    }
    if (!on) {
        remove_empty_dirs(dirs);
        if (FILE* f = dod3::open_file(mark, "wb")) {
            fputs("switched off: the files are in off/\n", f);
            fclose(f);
        } else {
            *err = "could not write " + utf8(mark);
            return false;
        }
    } else {
        fs::remove_all(off, ec);
        fs::remove(modfiles_dir(base) / id, ec);   /* when nothing was kept aside, the folder is empty */
        fs::remove(mark, ec);
    }
    return true;
}

const std::vector<DlcInfo>& known_mods()
{
    static const std::vector<DlcInfo> k = {
        { "UNDUB", "Japanese voices for the DLC chapters (DLC Undub)" },
        { "ES-GAME", "Spanish translation: the game" },
        { "ES-DATA", "Spanish translation: title screen and menus" },
        { "ES-MOVIES", "Spanish translation: movies" },
        { "ES-DLC", "Spanish translation: the DLC" },
        { "ES-JPV", "Spanish translation: the Japanese voices' movies" },
    };
    return k;
}

const std::vector<DlcInfo>& known_dlc()
{
    static const std::vector<DlcInfo> k = {
        { "ADDJAPANESEVOICE", "Japanese Voice Pack" },
        { "ADDSCENARIO00000", "Zero's Prologue" },
        { "ADDSCENARIO00001", "One's Prologue" },
        { "ADDSCENARIO00002", "Two's Prologue" },
        { "ADDSCENARIO00003", "Three's Prologue" },
        { "ADDSCENARIO00004", "Four's Prologue" },
        { "ADDSCENARIO00005", "Five's Prologue" },
        { "ADDSONG000000001", "Drakengard BGM Remix Pack" },
        { "ADDSONG000000002", "NieR BGM Remix Pack" },
        { "ADDSONG000000003", "BGM Remix Pack" },
        { "ADDWEAPONCLOTHE0", "Zero's Garb (Variety Pack)" },
        { "ADDWEAPONCLOTHE1", "Caim's Garb" },
        { "ADDWEAPONCLOTHE2", "Furiae's Garb" },
        { "ADDWEAPONCLOTHE3", "Manah's Garb" },
        { "ADDWEAPONCLOTHE4", "Eris's Garb" },
        { "ADDWEAPONCLOTHE5", "Nier's Garb" },
        { "ADDWEAPONCLOTHE6", "Kain\xC3\xA9's Garb" },
        { "ADDWEAPONCLOTHE7", "Beautiful Child" },
        { "ADDWEAPONCLOTHE8", "Tokyo Tower" },
        { "ADDWEAPONCLOTHE9", "Experimental Weapon 7" },
    };
    return k;
}

bool update_required() { return DOD3_EBOOT == 101; }
const char* eboot_path() { return DOD3_EBOOT == 101 ? "elf/EBOOT_101.ELF" : "elf/EBOOT.ELF"; }

uint64_t Plan::bytes() const
{
    uint64_t n = disc.bytes + update.bytes + eboot.bytes;
    for (const Source& d : dlc) n += d.bytes;
    for (const Source& m : mods) n += m.bytes;
    return n;
}

uint64_t free_bytes(const fs::path& base)
{
    std::error_code ec;
    fs::path p = fs::absolute(base, ec);
    while (!p.empty() && !fs::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    const fs::space_info s = fs::space(p, ec);
    return ec ? 0 : s.available;
}

Status installed(const fs::path& base)
{
    std::error_code ec;
    Status s;
    s.disc = disc_installed(base);
    s.update = fs::exists(update_dir(base) / "USRDIR/PATCH/SQEX03GAME/COOKEDPS3/COALESCED_INT.BIN", ec);
    s.eboot = fs::file_size(base / eboot_path(), ec) > 0 && !ec;
    s.eboot_bin = fs::exists(
        update_required() ? update_dir(base) / "USRDIR/EBOOT.BIN" : base / "game/disc/PS3_GAME/USRDIR/EBOOT.BIN", ec);
    for (const DlcInfo& d : known_dlc())
        if (fs::is_directory(dlc_dir(base) / "USRDIR/DLC" / d.id, ec)) s.dlc.insert(d.id);
    for (const auto& r : mods_read(base)) {
        if (r[1] == "jpv" && !fs::exists(dlc_dir(base) / "USRDIR/DLC_JPV/UNDUB/UNDUB.TXT", ec)) continue;
        s.mods.push_back({ r[0], r[2] });
    }
    return s;
}

bool gamedata_layout(const fs::path& root)
{
    std::error_code ec;
    const fs::path disc_game = root / "PS3_GAME/USRDIR/SQEX03GAME";
    if (!fs::is_directory(disc_game, ec)) return false;
    const fs::path data = root / "game/BLES00000DATA";
    const fs::path target = data / "USRDIR/FIOS-UNREALENGINE3/SQEX03GAME";
    static const char* const kFolders[] = { "COOKEDSOUND", "COOKEDPS3" };
    bool ok = true;
    fs::create_directories(target, ec);
    if (!fs::exists(data / "ICON0.PNG", ec)) fs::copy_file(root / "PS3_GAME/ICON0.PNG", data / "ICON0.PNG", ec);
    for (const char* f : kFolders) {
        const fs::path link = target / f, src = disc_game / f;
        if (is_link(link)) {   /* before is_directory: that follows the link */
            if (fs::is_directory(link, ec)) continue;
            fprintf(stderr, "[gamedata] %s points nowhere (was the folder moved?): linking it again\n", f);
            remove_link(link);
        } else if (fs::is_directory(link, ec)) {
            /* the title's own copy: whole, or cut short (the title would then
             * skip its install and miss the files) */
            if (count_files(link) >= count_files(src)) continue;
            fprintf(stderr, "[gamedata] %s is a copy that was cut short: replacing it with a link\n", f);
            const fs::path cut = target / (std::string(f) + ".partial");
            fs::rename(link, cut, ec);
            if (ec) {
                ok = false;
                break;
            }
            fs::remove_all(cut, ec);   /* a real folder, no links in it */
        }
        if (!make_link(src, link)) {
            ok = false;
            break;
        }
        fprintf(stderr, "[gamedata] %s: linked to the disc's (no copy needed)\n", f);
    }
    if (!ok) {
        /* no links here (exFAT, a network drive): leave no tree, so the title
         * installs the game data itself (about 8 minutes, once) */
        fprintf(stderr,
                "[gamedata] could not link the game data to the disc; the game will copy it on its first start\n");
        for (const char* f : kFolders)
            if (is_link(target / f)) remove_link(target / f);
        bool links = false;
        for (const char* f : kFolders) links |= is_link(target / f);
        if (!links) fs::remove_all(data, ec);
    }
    return ok;
}

bool key_art(const Source& game, std::vector<uint8_t>* png)
{
    std::string err;
    if (game.psn) {
        Pkg pkg;
        return pkg_open(game.path, &pkg, &err) && pkg_read_file(pkg, "PIC1.PNG", png, &err);
    }
    std::error_code ec;
    if (fs::is_directory(game.path, ec)) {   /* a folder: the file itself (open_disc would list the disc) */
        const fs::path p = game.path / "PS3_GAME/PIC1.PNG";
        const uint64_t n = fs::file_size(p, ec);
        if (ec || n == 0 || n > (64u << 20)) return false;
        png->resize((size_t)n);
        FILE* f = dod3::open_file(p, "rb");
        if (!f) return false;
        const bool ok = fread(png->data(), 1, png->size(), f) == png->size();
        fclose(f);
        return ok;
    }
    Disc disc;
    return open_disc(game.path, &disc, &err) && read_disc_file(disc, "PS3_GAME/PIC1.PNG", png);
}

bool identify(const fs::path& p, Source* s, std::string* err)
{
    std::error_code ec;
    *s = Source();
    if (fs::is_directory(p, ec)) {
        const fs::path dlc = undub_dlc_dir(p);
        if (!dlc.empty()) {
            if (!update_required()) {
                *err = "the DLC chapters' Japanese voices are for the 1.01 build";
                return false;
            }
            s->kind = SourceKind::Mod;
            s->form = ModForm::Undub;
            s->path = p;
            s->id = "UNDUB";
            s->name = mod_name("UNDUB");
            for (fs::recursive_directory_iterator it(dlc, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec)) s->bytes += it->file_size(ec);
            return true;
        }
        Disc disc;
        if (!open_disc(p, &disc, err)) return false;
        s->kind = SourceKind::Disc;
        s->path = disc.source;
        s->name = "Drakengard 3 disc (BLUS31197)";
        s->bytes = disc.total;
        return true;
    }
    if (!fs::is_regular_file(p, ec)) {
        *err = "not a file or a folder";
        return false;
    }
    s->path = p;
    const uint32_t m = magic(p);
    if (m == 0x7F504B47u) {   /* a PS3 package */
        const std::string cid = pkg_content_id(p);
        if (!retail(p)) {
            Pkg pkg;
            if (!pkg_open(p, &pkg, err)) return false;
            /* NoPayStation's "FIX" packages are debug packages too */
            if (licence_only(pkg)) {
                *err = "this is a licence fix package, not the DLC itself. It is not needed: add the DLC packages";
                return false;
            }
            /* a homebrew package: a mod, its files where mod_layout puts them */
            const std::string title = cid.size() >= 16 ? cid.substr(7, 9) : "";
            for (const PkgEntry& e : pkg.entries) {
                if (e.dir) continue;
                const std::string to = mod_layout(title, e.name);
                if (to == "?") {
                    *err = title == "BLUS31197" || title == "NPUB31251"
                               ? "a homebrew package with files the setup has no place for (" + e.name + ")"
                               : "a homebrew package for another game (" + cid + ")";
                    return false;
                }
                if (!to.empty()) s->bytes += e.size;
            }
            if (!s->bytes) {
                *err = "a homebrew package with nothing in it to install";
                return false;
            }
            if (!update_required()) {
                *err = "mods are for the 1.01 build";
                return false;
            }
            s->kind = SourceKind::Mod;
            s->form = ModForm::Pkg;
            if (const KnownPkg* k = known_pkg(pkg)) {
                s->id = k->id;
                s->name = mod_name(k->id);
            } else {
                s->id = "PKG-" + hex(pkg.qa, 4);
                s->name = "Mod package " + utf8(p.filename());
            }
            return true;
        }
        if (cid == kPsnGameId) {
            const std::string sha = pkg_footer_sha1(p);
            if (sha == kPsnUpdateSha1) {
                *err = "this is the PSN release's own update, which is not needed: both releases take the disc "
                       "release's 1.01 update, UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg";
                return false;
            }
            if (sha != kPsnGameSha1) {
                *err = "this is not the US PSN release's game package (version 1.00), or it is damaged";
                return false;
            }
            if (!update_required()) {
                *err = "this build plays version 1.00, made from the disc's executable; the PSN package is for the "
                       "1.01 build";
                return false;
            }
            Pkg pkg;
            if (!pkg_open(p, &pkg, err)) return false;
            s->kind = SourceKind::Disc;
            s->psn = true;
            s->id = cid;
            s->name = "Drakengard 3 PSN package (NPUB31251)";
            for (const PkgEntry& e : pkg.entries)
                if (!e.dir && !psn_layout(e.name).empty()) s->bytes += e.size;
            return true;
        }
        if (cid.rfind(kEuPrefix, 0) == 0) {
            *err = "this package is from the European PSN release (NPEB01407), a build of its own; only the US "
                   "release is supported, as the disc (BLUS31197) or the PSN package (NPUB31251)";
            return false;
        }
        if (cid == kUpdateId) {
            if (pkg_footer_sha1(p) != kUpdateSha1) {
                *err = "this update package is not version 1.01, or it is damaged";
                return false;
            }
            if (!update_required()) {
                *err = "this build plays version 1.00, which takes no update";
                return false;
            }
            Pkg pkg;
            if (!pkg_open(p, &pkg, err)) return false;
            s->kind = SourceKind::Update;
            s->id = cid;
            s->name = "Update 1.01";
            s->bytes = pkg.payload();
            return true;
        }
        if (cid.rfind(kDlcPrefix, 0) == 0) {
            const std::string id = cid.substr(sizeof kDlcPrefix - 1);
            for (const DlcInfo& d : known_dlc()) {
                if (id != d.id) continue;
                Pkg pkg;
                if (!pkg_open(p, &pkg, err)) return false;
                if (licence_only(pkg)) {
                    *err = "this is a licence fix package, not the DLC itself. It is not needed: add the " +
                           std::string(d.name) + " package";
                    return false;
                }
                s->kind = SourceKind::Dlc;
                s->id = id;
                s->name = d.name;
                s->bytes = pkg.payload();
                return true;
            }
            *err = "this Drakengard 3 package (" + id + ") is not one of the DLC packs";
            return false;
        }
        *err = cid.empty() ? "an unreadable PS3 package" : "a package for another game (" + cid + ")";
        return false;
    }
    if (m == 0x504B0304u) {   /* a .zip: a mod's movies */
        std::vector<ZipEntry> es;
        if (!zip_list(p, &es, err)) return false;
        if (!movie_entries(es)) {
            *err = "a .zip that is not a mod's movies: unpack it and add what is inside";
            return false;
        }
        if (!update_required()) {
            *err = "mods are for the 1.01 build";
            return false;
        }
        bool es_movies = true;
        size_t n = 0;
        for (const ZipEntry& e : es) {
            if (e.dir) continue;
            bool hit = false;
            for (const auto& k : kEsMovies)
                hit |= upper(fs::path(e.name).filename().string()) == k.name && e.crc == k.crc;
            es_movies &= hit;
            n++;
            s->bytes += e.usize;
        }
        s->kind = SourceKind::Mod;
        s->form = ModForm::Zip;
        if (es_movies && n == sizeof kEsMovies / sizeof kEsMovies[0]) {
            s->id = "ES-MOVIES";
            s->name = mod_name("ES-MOVIES");
        } else {
            s->id = "ZIP-" + upper(p.stem().string());
            s->name = "Movies " + utf8(p.filename());
        }
        return true;
    }
    if (m == 0x7F454C46u) {   /* an ELF */
        const std::string sha = sha256_file(p);
        const bool v101 = sha == kElf101Sha256, v100 = sha == kEbootElfSha256;
        if (update_required() ? v101 : v100) {
            s->kind = SourceKind::Eboot;
            s->name = std::string("EBOOT.ELF (") + DOD3_EBOOT_NAME + ")";
            s->bytes = fs::file_size(p, ec);
            return true;
        }
        if (v100)
            *err =
                "this is the disc's executable (1.00); this build plays 1.01, and makes its executable from the update";
        else if (v101)
            *err = "this is the update's executable (1.01); this build plays 1.00";
        else
            *err = "this EBOOT.ELF is not Drakengard 3's (BLUS31197) " DOD3_EBOOT_NAME;
        return false;
    }
    if (m == 0x53434500u) {   /* "SCE\0": a SELF */
        *err = update_required()
                   ? "EBOOT.BIN is not needed: the setup takes the game's executable from the update package"
                   : "EBOOT.BIN is not needed: the setup takes the game's executable from the disc";
        return false;
    }
    Disc disc;   /* a disc image */
    if (open_disc(p, &disc, err)) {
        s->kind = SourceKind::Disc;
        s->name = "Drakengard 3 disc image (BLUS31197)";
        s->bytes = disc.total;
        return true;
    }
    return false;
}

bool read_eboot_bin(const fs::path& base, const Plan& plan, std::vector<uint8_t>* self, std::string* err)
{
    std::error_code ec;
    fs::path file;
    if (update_required()) {
        if (plan.update.set()) {
            Pkg pkg;
            return pkg_open(plan.update.path, &pkg, err) && pkg_read_file(pkg, "USRDIR/EBOOT.BIN", self, err);
        }
        file = update_dir(base) / "USRDIR/EBOOT.BIN";
    } else {
        if (plan.disc.set()) {
            Disc disc;
            if (!open_disc(plan.disc.path, &disc, err)) return false;
            if (read_disc_file(disc, "PS3_GAME/USRDIR/EBOOT.BIN", self)) return true;
            *err = "cannot read the disc's EBOOT.BIN";
            return false;
        }
        file = base / "game/disc/PS3_GAME/USRDIR/EBOOT.BIN";
    }
    const uint64_t n = fs::file_size(file, ec);
    FILE* f = ec ? nullptr : dod3::open_file(file, "rb");
    if (!f || n > (256u << 20)) {
        if (f) fclose(f);
        *err = update_required() ? "the update is not installed" : "the disc is not installed";
        return false;
    }
    self->resize((size_t)n);
    const bool ok = fread(self->data(), 1, self->size(), f) == self->size();
    fclose(f);
    if (!ok) *err = "cannot read " + utf8(file);
    return ok;
}

bool make_elf(const std::vector<uint8_t>& self, const Keys& keys, std::vector<uint8_t>* elf, bool* wrong_keys,
              std::string* err)
{
    *wrong_keys = false;
    if (!keys.complete()) {
        *err = "the keys for the game's executable are missing";
        return false;
    }
    if (!self_decrypt(self, keys.self_keys(), elf, wrong_keys, err)) return false;
    const std::string sha = sha256_hex(elf->data(), elf->size());
    if (sha != (update_required() ? kElf101Sha256 : kEbootElfSha256)) {
        *err = "the executable made from EBOOT.BIN is not the expected one (SHA-256 " + sha + ")";
        return false;
    }
    return true;
}

bool install(const fs::path& base, const Plan& plan,
             const std::function<bool(uint64_t, uint64_t, const std::string&)>& progress, std::string* err)
{
    std::error_code ec;
    /* the progress total: the bytes read */
    uint64_t total = 0, done = 0;
    if (plan.disc.set()) total += plan.disc.psn ? fs::file_size(plan.disc.path, ec) : plan.disc.bytes;
    if (plan.eboot.set()) total += plan.eboot.bytes;
    if (plan.update.set()) total += fs::file_size(plan.update.path, ec);
    for (const Source& d : plan.dlc) total += fs::file_size(d.path, ec);
    for (const Source& m : plan.mods) total += m.form == ModForm::Undub ? m.bytes : fs::file_size(m.path, ec);
    const fs::path stage = staging(base);
    /* staging is deleted only once nothing of the previous copy is in it */
    auto clean = [&](std::string* why) {
        if (!settle_old(base, why)) return false;
        fs::remove_all(stage, ec);
        return true;
    };
    auto fail = [&](const std::string& e) {
        *err = e;
        std::string why;
        if (!clean(&why)) *err += ". Also, " + why;
        return false;
    };

    fs::create_directories(base / "game", ec);
    if (!clean(err)) return false;   /* a run that was cut short */
    if (plan.remove_mods) {
        if (!progress(0, total, "Removing the mods")) return fail("cancelled");
        if (!remove_mods(base, err)) return fail(*err);
    }
    for (const std::string& id : plan.remove_ids) {
        if (!progress(0, total, "Removing a mod")) return fail("cancelled");
        if (!remove_mod(base, id, err)) return fail(*err);
    }

    /* The executable, made first (in memory) so wrong keys stop the install
     * before anything is copied; written once the update is in place. */
    std::vector<uint8_t> elf;
    const bool make = !plan.eboot.set() && !installed(base).eboot;
    if (make) {
        if (!progress(0, total, "Making the game's executable")) return fail("cancelled");
        std::vector<uint8_t> self;
        bool wrong = false;
        if (!read_eboot_bin(base, plan, &self, err) || !make_elf(self, plan.keys, &elf, &wrong, err)) return fail(*err);
    }

    if (plan.disc.set() && (plan.disc.psn || !(fs::exists(base / "game/disc", ec) &&
                                               fs::equivalent(plan.disc.path, base / "game/disc", ec)))) {
        const fs::path part = stage / "disc", old = stage / "old", final_dir = base / "game/disc";
        auto step = [&](uint64_t d, uint64_t) { return progress(done + d, total, "Copying the game"); };
        if (plan.disc.psn) {
            /* the PSN package, laid out as the disc */
            Pkg pkg;
            if (!pkg_open(plan.disc.path, &pkg, err) || !pkg_extract(pkg, part, step, err, psn_layout) ||
                !psn_fix_toc(part, err))
                return fail(*err);
        } else {
            Disc disc;
            if (!open_disc(plan.disc.path, &disc, err) || !copy_disc(disc, part, step, err)) return fail(*err);
        }
        /* The swap: the old copy aside, the new one in, then what the old one
         * held besides the disc back (settle_old). Nothing that may hold the
         * game data's links onto the disc is deleted on the way. */
        if (fs::exists(final_dir, ec)) {
            move_patiently(final_dir, old, ec);
            if (ec) return fail("could not replace the game's files: " + ec.message());
        }
        move_patiently(part, final_dir, ec);
        if (ec)
            return fail("could not move the game into place: " + ec.message()); /* settle_old puts the old copy back */
        if (!settle_old(base, err)) return false;
    }
    if (plan.disc.set()) {
        done += plan.disc.psn ? fs::file_size(plan.disc.path, ec) : plan.disc.bytes;
        gamedata_layout(base / "game/disc");
    }

    if (plan.eboot.set()) {
        if (!progress(done, total, "Copying EBOOT.ELF")) return fail("cancelled");
        const fs::path part = stage / "EBOOT.ELF", to = base / eboot_path();
        fs::create_directories(stage, ec);
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(plan.eboot.path, part, fs::copy_options::overwrite_existing, ec);
        if (!ec) move_patiently(part, to, ec);
        if (ec) return fail("could not copy EBOOT.ELF: " + ec.message());
        done += plan.eboot.bytes;
    }

    if (plan.update.set()) {
        Pkg pkg;
        if (!pkg_open(plan.update.path, &pkg, err)) return fail(*err);
        const fs::path part = stage / "update";
        const uint64_t start = done;
        if (!pkg_extract(
                pkg, part, [&](uint64_t d, uint64_t) { return progress(start + d, total, "Installing update 1.01"); },
                err))
            return fail(*err);
        /* the whole package, as the console installs it (EBOOT.BIN too, so
         * the executable can be made again); the old PATCH folder goes whole,
         * so a file 1.01 dropped does not linger */
        fs::remove_all(update_dir(base) / "USRDIR/PATCH", ec);
        {   /* the mods that had files there are gone with them, their lists
             * and the files they kept aside too (one's files in game/mods stay) */
            auto rows = mods_read(base);
            const size_t n = rows.size();
            rows.erase(std::remove_if(rows.begin(), rows.end(),
                                      [&](const auto& r) {
                                          if (r[1].find("patch") == std::string::npos) return false;
                                          fs::remove(modfiles_dir(base) / (r[0] + ".txt"), ec);
                                          fs::remove(modfiles_dir(base) / (r[0] + ".off"), ec);
                                          fs::remove_all(modfiles_dir(base) / r[0], ec);
                                          return true;
                                      }),
                       rows.end());
            if (rows.size() != n) mods_write(base, rows);
        }
        if (!merge_move(part, update_dir(base), err)) return fail(*err);
        done = start + pkg.file_size;
    }

    if (make) {
        if (!progress(done, total, "Writing the game's executable")) return fail("cancelled");
        const fs::path part = stage / "EBOOT.ELF", to = base / eboot_path();
        fs::create_directories(stage, ec);
        fs::create_directories(to.parent_path(), ec);
        FILE* f = dod3::open_file(part, "wb");
        const bool wrote = f && fwrite(elf.data(), 1, elf.size(), f) == elf.size();
        if (f) fclose(f);
        if (wrote) move_patiently(part, to, ec);
        if (!wrote || ec) return fail("could not write " + utf8(to));
        if (!builtin_keys().complete()) keys_save(keys_file(base), plan.keys); /* the player's, for the next time */
    }

    for (const Source& d : plan.dlc) {
        Pkg pkg;
        if (!pkg_open(d.path, &pkg, err)) return fail(*err);
        const fs::path part = stage / "dlc";
        const uint64_t start = done;
        const std::string what = "Installing " + d.name;
        if (!pkg_extract(pkg, part, [&](uint64_t x, uint64_t) { return progress(start + x, total, what); }, err))
            return fail(*err);
        /* each pack is its own folder under USRDIR/DLC; the voice pack also
         * fills USRDIR/DLC_JPV. The shared PARAM.SFO and icons are the same
         * in every pack. */
        if (!merge_move(part, dlc_dir(base), err)) return fail(*err);
        if (d.id == "ADDJAPANESEVOICE" && !dlc_write_jpv_lists(base / "game/disc", true))
            return fail("the Japanese Voice Pack installed, but its file lists could not be written");
        done = start + pkg.file_size;
    }

    for (const Source& md : plan.mods) {
        const uint64_t start = done;
        const std::string what = "Installing " + md.name;
        if (!install_mod(base, md, stage, [&](uint64_t x) { return progress(start + x, total, what); }, err))
            return fail(*err);
        done = start + (md.form == ModForm::Undub ? md.bytes : fs::file_size(md.path, ec));
    }
    fs::remove_all(stage, ec);
    progress(total, total, "Done");
    return true;
}

bool needs_voice_pack(const Plan& plan, const Status& have)
{
    bool undub = false, pack = have.dlc.count("ADDJAPANESEVOICE") != 0;
    for (const Source& m : plan.mods) undub |= m.form == ModForm::Undub;
    for (const Source& d : plan.dlc) pack |= d.id == "ADDJAPANESEVOICE";
    return undub && !pack;
}

int install_files_cli(const fs::path& base, const std::vector<fs::path>& files, const fs::path& keys_path)
{
    Plan plan;
    {
        std::error_code ec;
        const fs::path kp = keys_path.empty() ? keys_file(base) : keys_path;
        std::string err;
        if (keys_path.empty() && builtin_keys().complete()) {
            plan.keys = builtin_keys();
        } else if ((!keys_path.empty() || fs::exists(kp, ec)) && !keys_load(kp, &plan.keys, &err)) {
            printf("%s: %s\n", utf8(kp).c_str(), err.c_str());
            return 1;
        }
    }
    for (const fs::path& f : files) {
        if (f == "--remove-mods") {
            plan.remove_mods = true;
            printf("--remove-mods: the installed mods are removed\n");
            continue;
        }
        Source s;
        std::string err;
        if (!identify(f, &s, &err)) {
            printf("%s: %s\n", utf8(f).c_str(), err.c_str());
            return 1;
        }
        printf("%s: %s\n", utf8(f).c_str(), s.name.c_str());
        switch (s.kind) {
        case SourceKind::Disc: plan.disc = s; break;
        case SourceKind::Update: plan.update = s; break;
        case SourceKind::Eboot: plan.eboot = s; break;
        case SourceKind::Dlc:
            plan.dlc.erase(
                std::remove_if(plan.dlc.begin(), plan.dlc.end(), [&](const Source& o) { return o.id == s.id; }),
                plan.dlc.end());
            plan.dlc.push_back(s);
            break;
        case SourceKind::Mod:
            plan.mods.erase(
                std::remove_if(plan.mods.begin(), plan.mods.end(), [&](const Source& o) { return o.id == s.id; }),
                plan.mods.end());
            plan.mods.push_back(s);
            break;
        default: break;
        }
    }
    const Status have = installed(base);
    std::string missing;
    if (!plan.disc.set() && !have.disc)
        missing += update_required() ? " the game (the disc as an .iso or its folder, or the US PSN package),"
                                     : " the game disc (an .iso or its folder),";
    if (update_required() && !plan.update.set() && !have.update) missing += " the 1.01 update package,";
    if (plan.remove_mods && !plan.update.set() && mods_need_update(base))
        missing += " the 1.01 update package (it puts back the files a mod replaced in its folder),";
    if (needs_voice_pack(plan, have))
        missing += " the Japanese Voice Pack's package (the DLC chapters' Japanese voices go with it),";
    if (!plan.eboot.set() && !have.eboot && !plan.keys.complete()) {
        missing += " the keys for the game's executable (--keys <file> with";
        for (const KeyField& k : needed_keys()) missing += std::string(" ") + k.id;
        missing += "),";
    }
    if (!missing.empty()) {
        missing.pop_back();
        printf("still needed:%s\n", missing.c_str());
        return 1;
    }
    const uint64_t need = plan.bytes(), space = free_bytes(base);
    printf("installing %.2f GB into %s (%.1f GB free)\n", need / 1073741824.0, utf8(base).c_str(),
           space / 1073741824.0);
    if (space && need > space) {
        printf("not enough space\n");
        return 1;
    }
    int last = -1;
    std::string step, err;
    const bool ok = install(
        base, plan,
        [&](uint64_t d, uint64_t t, const std::string& what) {
            const int pct = t ? (int)(d * 100 / t) : 100;
            if (pct != last || what != step) {
                if (what != step && !step.empty()) printf("\n");
                last = pct;
                step = what;
                printf("\r%-40s %3d%%", what.c_str(), pct);
                fflush(stdout);
            }
            return true;
        },
        &err);
    printf("\n");
    if (!ok) {
        printf("install failed: %s\n", err.c_str());
        return 1;
    }
    printf("installed\n");
    return 0;
}

}  // namespace dod3setup
