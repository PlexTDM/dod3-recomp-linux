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
#include <chrono>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <thread>

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

/* The DLC packs and mods the setup handles by name. */
const char kVoicePackId[] = "ADDJAPANESEVOICE";
const char kUndubId[] = "UNDUB";
const int kChapters = 6;   /* the DLC chapters: ADDSCENARIO00000 (Zero's) to 00005 (Five's) */

using dod3::utf8;

fs::path update_dir(const fs::path& base) { return base / "game/disc/game/BLES00000"; }
fs::path dlc_dir(const fs::path& base) { return base / "game/disc/game/NPUB31251"; }
fs::path jpv_dir(const fs::path& base) { return dlc_dir(base) / "USRDIR/DLC_JPV"; }   /* the voice pack's own folder */
fs::path mods_dir(const fs::path& base) { return base / "game/mods"; }
/* each mod's file list and the update's files it replaced (install.h) */
fs::path modfiles_dir(const fs::path& base) { return base / "game/modfiles"; }
fs::path staging(const fs::path& base) { return base / "game/setup.partial"; }

/* a DLC chapter's folder name: "ADDSCENARIO0000" and its number */
std::string chapter_dir(int i) { return "ADDSCENARIO0000" + std::to_string(i); }

/* the title ID in a content ID: "UP0082-BLUS31197_00-..." -> "BLUS31197"; "" if it is too short */
std::string cid_title(const std::string& cid) { return cid.size() >= 16 ? cid.substr(7, 9) : ""; }

/* a file's first four bytes, big-endian */
uint32_t magic(const fs::path& p)
{
    FILE* f = dod3::open_file(p, "rb");
    uint8_t m[4] = {};
    if (f) {
        if (fread(m, 1, 4, f) != 4) m[0] = 0;
        fclose(f);
    }
    return dod3_be32(m);
}

/* A whole file into memory; false if it cannot be read or is over `max` bytes. */
bool read_file(const fs::path& p, uint64_t max, std::vector<uint8_t>* out)
{
    std::error_code ec;
    const uint64_t n = fs::file_size(p, ec);
    if (ec || n > max) return false;
    FILE* f = dod3::open_file(p, "rb");
    if (!f) return false;
    out->resize((size_t)n);
    const bool ok = fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

/* A whole file written; false if it could not be. */
bool write_file(const fs::path& p, const void* data, size_t n)
{
    FILE* f = dod3::open_file(p, "wb");
    if (!f) return false;
    const bool wrote = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && wrote;
}
bool write_file(const fs::path& p, const std::string& s) { return write_file(p, s.data(), s.size()); }

/* A text file's lines, without their line ends; empty if it cannot be read. */
std::vector<std::string> read_lines(const fs::path& p)
{
    std::vector<std::string> lines;
    std::vector<uint8_t> d;
    if (!read_file(p, 64u << 20, &d)) return lines;
    std::string l;
    for (uint8_t c : d) {
        if (c == '\n') {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            l.clear();
        } else
            l += (char)c;
    }
    if (!l.empty()) lines.push_back(l);
    return lines;
}

/* fs::rename, tried again for a few seconds while it is refused: right after
 * thousands of files are written, a file scanner or the search indexer may
 * still hold one of them open, and the folder cannot move until it lets go. */
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

/* Is `p` a symlink, whatever it points at? */
bool is_link(const fs::path& p)
{
    std::error_code ec;
    return fs::is_symlink(fs::symlink_status(p, ec));
}

/* Remove the link itself, never what it points at. */
bool remove_link(const fs::path& p)
{
    std::error_code ec;
    return fs::remove(p, ec);
}

/* A directory symlink at `link` onto `target`, relative so the tree can be
 * moved; a link that points nowhere is repaired at the next boot. */
bool make_link(const fs::path& target, const fs::path& link)
{
    std::error_code ec;
    fs::create_directory_symlink(fs::relative(target, link.parent_path(), ec), link, ec);
    return !ec;
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
    bool licences = false;
    for (const PkgEntry& e : pkg.entries) {
        if (e.dir || !e.name.starts_with("USRDIR/")) continue;
        if (!e.name.ends_with(".EDAT")) return false;
        licences = true;
    }
    return licences;
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

const KnownPkg* known_pkg(const Pkg& pkg)
{
    for (const KnownPkg& k : kKnownPkgs)
        if (pkg.file_size == k.size && to_hex(pkg.qa, 16) == k.qa) return &k;
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
 * PARAM.SFO, icons, package.conf); nothing if it has nowhere to go. */
std::optional<std::string> mod_layout(const std::string& title, const std::string& name)
{
    static const char kPatch[] = "USRDIR/PATCH/";
    if (name.find('/') == std::string::npos) return "";
    if (title == "BLUS31197") {
        if (name.starts_with(kPatch)) return "patch/" + name.substr(sizeof kPatch - 1);
        if (name.starts_with("USRDIR/FIOS-UNREALENGINE3/")) return "tree/game/BLES00000DATA/" + name;
    } else if (title == "NPUB31251") {
        if (name.starts_with("USRDIR/DLC/") || name.starts_with("USRDIR/DLC_JPV/")) return "tree/game/NPUB31251/" + name;
    }
    return std::nullopt;
}

/* A movie archive's entries: every file a .BIK, in MOVIES/ or at the top. */
bool movie_entries(const std::vector<ZipEntry>& es)
{
    size_t n = 0;
    for (const ZipEntry& e : es) {
        if (e.dir) continue;
        const std::string u = upper(e.name);
        const size_t sl = u.find('/');
        if (!u.ends_with(".BIK")) return false;
        if (sl != std::string::npos && !u.starts_with("MOVIES/")) return false;
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
        for (int i = 0; i < kChapters; i++)
            if (fs::is_directory(d / chapter_dir(i), ec)) return d;
    }
    return {};
}

/* The release the DLC Undub's readme names in its first line ("Drakengard 3
 * DLC Undub Prototype ver.... by ..."), else "". */
std::string undub_release(const fs::path& folder)
{
    const std::vector<std::string> lines = read_lines(folder / "Readme.txt");
    std::string l = lines.empty() ? "" : lines[0];
    while (!l.empty() && l.back() == ' ') l.pop_back();
    return l.find("Undub") != std::string::npos && l.size() < 160 ? l : std::string();
}

/* game/mods/installed.txt: one line a mod, "<id>\t<where>\t<name>"; where is
 * "patch" (the update's folder), "tree" (game/mods), "jpv" (the voice pack's
 * folder), or a comma-separated mix. */
struct ModRow {
    std::string id, where, name;
    bool in_patch() const { return where.find("patch") != std::string::npos; }
};

std::vector<ModRow> mods_read(const fs::path& base)
{
    std::vector<ModRow> rows;
    for (const std::string& l : read_lines(mods_dir(base) / "installed.txt")) {
        const size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
        if (b != std::string::npos) rows.push_back({ l.substr(0, a), l.substr(a + 1, b - a - 1), l.substr(b + 1) });
    }
    return rows;
}

bool mods_write(const fs::path& base, const std::vector<ModRow>& rows)
{
    std::error_code ec;
    fs::create_directories(mods_dir(base), ec);
    std::string s;
    for (const ModRow& r : rows) s += r.id + "\t" + r.where + "\t" + r.name + "\n";
    return write_file(mods_dir(base) / "installed.txt", s);
}

bool mods_write(const fs::path& base, const std::vector<ModRow>& rows, std::string* err)
{
    if (mods_write(base, rows)) return true;
    *err = "could not write the list of installed mods (game/mods/installed.txt)";
    return false;
}

/* A mod's line taken out of the list. */
bool mods_forget(const fs::path& base, const std::string& id, std::string* err)
{
    auto rows = mods_read(base);
    std::erase_if(rows, [&](const ModRow& r) { return r.id == id; });
    return mods_write(base, rows, err);
}

/* A mod's line written, in place of any it had. */
bool mods_record(const fs::path& base, const ModRow& row, std::string* err)
{
    auto rows = mods_read(base);
    std::erase_if(rows, [&](const ModRow& r) { return r.id == row.id; });
    rows.push_back(row);
    return mods_write(base, rows, err);
}

/* Where the PSN game package's files go: under PS3_GAME, as on the disc. Its
 * EBOOT.BIN and its one .EDAT (COALESCED_INT.BIN, under licence) are left
 * out; the update brings the executable and the plain file. */
std::string psn_layout(const std::string& name)
{
    if (name == "USRDIR/EBOOT.BIN" || name.ends_with(".EDAT")) return "";
    return "PS3_GAME/" + name;
}

/* The PSN package's file list (PS3TOC.TXT) names COALESCED_INT.BIN as its
 * .EDAT, where the disc's names the plain file; with that line as on the
 * disc, the list is the disc's own (checked by its hash). */
bool psn_fix_toc(const fs::path& disc, std::string* err)
{
    const fs::path toc = disc / "PS3_GAME/USRDIR/SQEX03GAME/PS3TOC.TXT";
    std::vector<uint8_t> d;
    const bool read = read_file(toc, 16u << 20, &d);
    std::string s(d.begin(), d.end());
    const std::string from = "\\CookedPS3\\COALESCED_INT.BIN.EDAT ", to = "\\CookedPS3\\Coalesced_INT.bin ";
    const size_t at = s.find(from);
    if (read && at != std::string::npos) s.replace(at, from.size(), to);
    if (!read || sha256_hex(s.data(), s.size()) != kDiscTocSha256) {
        *err = "the PSN package's file list (PS3TOC.TXT) is not the one expected";
        return false;
    }
    if (write_file(toc, s)) return true;
    *err = "could not write " + utf8(toc);
    return false;
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
    const fs::path jpv = jpv_dir(base);
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
    for (int i = 0; i < kChapters; i++) {
        for (fs::recursive_directory_iterator it(src / chapter_dir(i), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const bool movie = upper(utf8(it->path().parent_path().filename())) == "MOVIES";
            items.push_back({ it->path(), i, movie });
            if (!movie) chapters_of[upper(utf8(it->path().filename()))]++;
        }
    }
    if (items.empty()) {
        *err = "no DLC chapter files in " + utf8(src);
        return false;
    }
    /* a name in more than one chapter, or one the main game or the voice pack has too */
    const fs::path cooked = base / "game/disc/PS3_GAME/USRDIR/SQEX03GAME/COOKEDPS3";
    auto shared = [&](const fs::path& f) {
        const auto it = chapters_of.find(upper(utf8(f.filename())));
        return (it != chapters_of.end() && it->second > 1) || fs::exists(cooked / f.filename(), ec) ||
               fs::exists(jpv / "SQEX03GAME/COOKEDPS3" / f.filename(), ec);
    };
    const fs::path part = stage / "UNDUB";
    fs::remove_all(part, ec);
    std::set<std::string> by_chapter;
    uint64_t done = 0;
    for (const Item& it : items) {
        const fs::path n = it.from.filename();
        fs::path to;
        if (it.movie)
            to = part / "MOVIES" / n;
        else if (shared(it.from)) {
            to = part / chapter_dir(it.chapter) / n;
            by_chapter.insert(upper(utf8(n)));
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
    if (!write_file(part / "UNDUB.TXT", txt)) {   /* the install is told by it (installed, the voice lists) */
        *err = "could not write " + utf8(part / "UNDUB.TXT");
        return false;
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
    return mods_record(base, { s.id, "jpv", s.name }, err);
}

/* A mod's list of its files (game/modfiles/<id>.txt): "patch/<path>" in the
 * update's PATCH folder, "tree/<path>" in game/mods, with "\t1" after it when
 * the update's own file there was kept aside (game/modfiles/<id>/orig/<path>).
 * Switched off (set_mod_enabled), the mod's files are in
 * game/modfiles/<id>/off/<kind>/<path> and <id>.off says so. */
struct ModFile {
    std::string kind;   /* "patch" or "tree" */
    fs::path rel;
    bool kept = false;
    std::string line() const
    {
        const auto g = rel.generic_u8string();
        return kind + "/" + std::string(g.begin(), g.end()) + (kept ? "\t1\n" : "\n");
    }
};

fs::path list_file(const fs::path& base, const std::string& id) { return modfiles_dir(base) / (id + ".txt"); }
fs::path off_mark(const fs::path& base, const std::string& id) { return modfiles_dir(base) / (id + ".off"); }
bool has_list(const fs::path& base, const std::string& id)
{
    std::error_code ec;
    return fs::exists(list_file(base, id), ec);
}

bool mod_files(const fs::path& base, const std::string& id, std::vector<ModFile>* out)
{
    if (!has_list(base, id)) return false;
    for (std::string l : read_lines(list_file(base, id))) {
        ModFile m;
        m.kept = l.ends_with("\t1");
        if (m.kept) l.resize(l.size() - 2);
        const size_t sl = l.find('/');
        if (sl == std::string::npos || l.find("..") != std::string::npos) continue;
        m.kind = l.substr(0, sl);
        m.rel = dod3::path_from_utf8(l.substr(sl + 1));
        if ((m.kind != "patch" && m.kind != "tree") || m.rel.empty() || m.rel.is_absolute()) continue;
        out->push_back(m);
    }
    return true;
}

fs::path mod_root(const fs::path& base, const std::string& kind)
{
    return kind == "patch" ? update_dir(base) / "USRDIR/PATCH" : mods_dir(base);
}

/* each folder above a mod's files, for remove_empty_dirs */
void add_dirs(std::set<fs::path>& dirs, const fs::path& root, const fs::path& rel)
{
    for (fs::path d = rel.parent_path(); !d.empty(); d = d.parent_path()) dirs.insert(root / d);
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
    if (id == kUndubId) {
        const fs::path jpv = jpv_dir(base);
        fs::remove_all(jpv / "UNDUB", ec);
        if (ec) {
            *err = "could not remove " + utf8(jpv / "UNDUB") + ": " + ec.message();
            return false;
        }
        if (fs::is_directory(jpv / "SQEX03GAME", ec) && !dlc_write_jpv_lists(base / "game/disc", true)) {
            *err = "could not write the voice pack's file lists again";
            return false;
        }
        return mods_forget(base, id, err);
    }
    const fs::path orig = modfiles_dir(base) / id / "orig", off = modfiles_dir(base) / id / "off";
    std::vector<ModFile> list;
    if (!mod_files(base, id, &list)) {
        *err = "the files of " + id + " were not listed when it was installed: add the update package and remove "
               "all the mods";
        return false;
    }
    std::set<fs::path> dirs;
    for (const ModFile& m : list) {
        const fs::path root = mod_root(base, m.kind), live = root / m.rel;
        if (m.kept) {
            /* the update's own back over the mod's -- unless it is back already
             * (switched off, or a removal cut short) */
            if (fs::exists(orig / m.rel, ec)) {
                move_patiently(orig / m.rel, live, ec);
                if (ec) {   /* the list and what it kept aside stay: removing it again finishes */
                    *err = "could not put back " + utf8(live) + ": " + ec.message();
                    return false;
                }
            }
        } else if (!fs::exists(off / m.kind / m.rel, ec)) {
            fs::remove(live, ec);   /* the mod's own file (switched off, it is in off/) */
        }
        add_dirs(dirs, root, m.rel);
    }
    remove_empty_dirs(dirs);
    fs::remove_all(modfiles_dir(base) / id, ec);
    fs::remove(list_file(base, id), ec);
    fs::remove(off_mark(base, id), ec);
    return mods_forget(base, id, err);
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
        const std::string title = cid_title(pkg.content_id);
        if (!pkg_extract(pkg, part, [&](uint64_t x, uint64_t) { return progress(x); }, err,
                         [&](const std::string& n) { return mod_layout(title, n).value_or(""); }))
            return false;
    }
    /* The same mod installed before: its files go first (the update's own
     * put back), so what it replaces now is the game's. One installed before
     * the lists were kept, with files in the update's folder, is installed
     * over without a list: removing it still takes the update. */
    const bool had_list = has_list(base, s.id);
    bool listed = true;   /* this install keeps a list of its files */
    if (!had_list)
        for (const ModRow& r : mods_read(base))
            if (r.id == s.id && r.in_patch()) listed = false;
    if (had_list && !remove_mod(base, s.id, err)) return false;
    /* The list is written before anything moves in, the update's files it
     * replaces kept aside first: a move cut short is undone by Remove. */
    std::vector<ModFile> files;
    auto list_files = [&](const fs::path& from, const char* kind, const fs::path& to, const fs::path& orig) {
        for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            ModFile m{ kind, fs::relative(it->path(), from, ec) };
            if (!orig.empty() && fs::exists(to / m.rel, ec)) {   /* the update's own: kept aside */
                fs::create_directories((orig / m.rel).parent_path(), ec);
                move_patiently(to / m.rel, orig / m.rel, ec);
                m.kept = !ec;
                ec.clear();
            }
            files.push_back(m);
        }
    };
    const fs::path patch = mod_root(base, "patch");
    const bool has_patch = fs::exists(part / "patch", ec), has_tree = fs::exists(part / "tree", ec);
    if (has_patch && !installed(base).update) {
        *err = s.name + " goes into the 1.01 update's folder: add the update package too";
        return false;
    }
    if (listed) {
        if (has_patch) list_files(part / "patch", "patch", patch, modfiles_dir(base) / s.id / "orig");
        if (has_tree) list_files(part / "tree", "tree", mods_dir(base), fs::path());
        std::string list;
        for (const ModFile& m : files) list += m.line();
        fs::create_directories(modfiles_dir(base), ec);
        if (!write_file(list_file(base, s.id), list)) {
            *err = "could not write " + utf8(list_file(base, s.id));
            return false;
        }
    }
    std::string where;
    if (has_patch) {
        if (!merge_move(part / "patch", patch, err)) return false;
        where = "patch";
    }
    if (has_tree) {
        if (!merge_move(part / "tree", mods_dir(base), err)) return false;
        where += where.empty() ? "tree" : ",tree";
    }
    fs::remove_all(part, ec);
    return mods_record(base, { s.id, where, s.name }, err);
}

/* The installed mods removed (Plan::remove_mods): each listed one by itself
 * (the update's own files put back), then the rest of game/mods' files (the
 * setup's own, no links there) and the DLC chapters' voices. A mod with files
 * in the update's folder but no list stays listed until the update is
 * installed again, which puts its own files back. */
bool remove_mods(const fs::path& base, std::string* err)
{
    std::error_code ec;
    for (const ModRow& r : mods_read(base))
        if ((r.id == kUndubId || has_list(base, r.id)) && !remove_mod(base, r.id, err)) return false;
    for (fs::directory_iterator it(mods_dir(base), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename() != "installed.txt") fs::remove_all(it->path(), ec);
    if (fs::exists(jpv_dir(base) / "UNDUB", ec) && !remove_mod(base, kUndubId, err)) return false;
    auto rows = mods_read(base);
    std::erase_if(rows, [](const ModRow& r) { return !r.in_patch(); });
    for (ModRow& r : rows) r.where = "patch";
    return mods_write(base, rows, err);
}

/* The update installed again (its PATCH folder replaced whole): the mods'
 * files there went with the old folder, and the update's own files they
 * kept aside are the old update's. A mod with files only there is
 * forgotten; one with files in game/mods too is kept, as those. */
void forget_patch_mods(const fs::path& base)
{
    std::error_code ec;
    auto rows = mods_read(base);
    bool changed = false;
    for (auto it = rows.begin(); it != rows.end();) {
        if (!it->in_patch()) {
            ++it;
            continue;
        }
        changed = true;
        const std::string id = it->id;
        fs::remove_all(modfiles_dir(base) / id / "orig", ec);
        fs::remove_all(modfiles_dir(base) / id / "off" / "patch", ec);
        if (it->where.find("tree") == std::string::npos) {
            fs::remove(list_file(base, id), ec);
            fs::remove(off_mark(base, id), ec);
            fs::remove_all(modfiles_dir(base) / id, ec);
            it = rows.erase(it);
            continue;
        }
        std::vector<ModFile> files;
        if (mod_files(base, id, &files)) {
            std::string list;
            for (const ModFile& m : files)
                if (m.kind == "tree") list += m.line();
            write_file(list_file(base, id), list);
        }
        it->where = "tree";
        ++it;
    }
    if (changed) mods_write(base, rows);
}

}  // namespace

bool mods_need_update(const fs::path& base)
{
    for (const ModRow& r : mods_read(base))
        if (r.in_patch() && !has_list(base, r.id)) return true;
    return false;
}

bool mod_removable(const fs::path& base, const std::string& id) { return id == kUndubId || has_list(base, id); }

bool mod_switchable(const fs::path& base, const std::string& id) { return id != kUndubId && has_list(base, id); }

bool mod_enabled(const fs::path& base, const std::string& id)
{
    std::error_code ec;
    return !fs::exists(off_mark(base, id), ec);
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
                   mark = off_mark(base, id);
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
            add_dirs(dirs, root, m.rel);
        } else if (fs::exists(aside, ec)) {
            if (m.kept && fs::exists(live, ec) && !fs::exists(own, ec) && !move(live, own)) return false;
            if (!fs::exists(live, ec) && !move(aside, live)) return false;
        }
    }
    if (!on) {
        remove_empty_dirs(dirs);
        if (!write_file(mark, std::string("switched off: the files are in off/\n"))) {
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
    for (const ModRow& r : mods_read(base)) {
        if (r.where == "jpv" && !fs::exists(jpv_dir(base) / "UNDUB/UNDUB.TXT", ec)) continue;
        s.mods.push_back({ r.id, r.name });
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
    if (fs::is_directory(game.path, ec))   /* a folder: the file itself (open_disc would list the disc) */
        return read_file(game.path / "PS3_GAME/PIC1.PNG", 64u << 20, png) && !png->empty();
    Disc disc;
    return open_disc(game.path, &disc, &err) && read_disc_file(disc, "PS3_GAME/PIC1.PNG", png);
}

namespace {

/* A homebrew (debug) package: a mod, its files where mod_layout puts them.
 * NoPayStation's licence "FIX" packages are debug packages too. */
bool identify_mod_pkg(const fs::path& p, const std::string& cid, Source* s, std::string* err)
{
    Pkg pkg;
    if (!pkg_open(p, &pkg, err)) return false;
    if (licence_only(pkg)) {
        *err = "this is a licence fix package, not the DLC itself. It is not needed: add the DLC packages";
        return false;
    }
    const std::string title = cid_title(cid);
    for (const PkgEntry& e : pkg.entries) {
        if (e.dir) continue;
        const std::optional<std::string> to = mod_layout(title, e.name);
        if (!to) {
            *err = title == "BLUS31197" || title == "NPUB31251"
                       ? "a homebrew package with files the setup has no place for (" + e.name + ")"
                       : "a homebrew package for another game (" + cid + ")";
            return false;
        }
        if (!to->empty()) s->bytes += e.size;
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
        s->id = "PKG-" + to_hex(pkg.qa, 4);
        s->name = "Mod package " + utf8(p.filename());
    }
    return true;
}

/* A PS3 package: the US PSN game, the update, a DLC pack, or a mod. */
bool identify_pkg(const fs::path& p, Source* s, std::string* err)
{
    const std::string cid = pkg_content_id(p);
    if (pkg_is_debug(p)) return identify_mod_pkg(p, cid, s, err);
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
    if (cid.starts_with(kEuPrefix)) {
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
    if (cid.starts_with(kDlcPrefix)) {
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

/* A .zip: a mod's movies (the Spanish translation's MOVIES.zip, or others). */
bool identify_zip(const fs::path& p, Source* s, std::string* err)
{
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
        const std::string name = upper(utf8(dod3::path_from_utf8(e.name).filename()));
        es_movies &= std::any_of(std::begin(kEsMovies), std::end(kEsMovies),
                                 [&](const auto& k) { return name == k.name && e.crc == k.crc; });
        n++;
        s->bytes += e.usize;
    }
    s->kind = SourceKind::Mod;
    s->form = ModForm::Zip;
    if (es_movies && n == std::size(kEsMovies)) {
        s->id = "ES-MOVIES";
        s->name = mod_name("ES-MOVIES");
    } else {
        s->id = "ZIP-" + upper(utf8(p.stem()));
        s->name = "Movies " + utf8(p.filename());
    }
    return true;
}

/* An ELF: the game's executable, made already (this build's version). */
bool identify_elf(const fs::path& p, Source* s, std::string* err)
{
    std::error_code ec;
    const std::string sha = sha256_file(p);
    const bool v101 = sha == kElf101Sha256, v100 = sha == kEbootElfSha256;
    if (update_required() ? v101 : v100) {
        s->kind = SourceKind::Eboot;
        s->name = std::string("EBOOT.ELF (") + DOD3_EBOOT_NAME + ")";
        s->bytes = fs::file_size(p, ec);
        return true;
    }
    if (v100)
        *err = "this is the disc's executable (1.00); this build plays 1.01, and makes its executable from the update";
    else if (v101)
        *err = "this is the update's executable (1.01); this build plays 1.00";
    else
        *err = "this EBOOT.ELF is not Drakengard 3's (BLUS31197) " DOD3_EBOOT_NAME;
    return false;
}

}  // namespace

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
            s->id = kUndubId;
            s->name = mod_name(kUndubId);
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
    switch (magic(p)) {
    case kPkgMagic: return identify_pkg(p, s, err);
    case 0x504B0304u: return identify_zip(p, s, err);   /* "PK\3\4" */
    case 0x7F454C46u: return identify_elf(p, s, err);   /* "\x7F" "ELF" */
    case 0x53434500u:                                    /* "SCE\0": a SELF */
        *err = update_required()
                   ? "EBOOT.BIN is not needed: the setup takes the game's executable from the update package"
                   : "EBOOT.BIN is not needed: the setup takes the game's executable from the disc";
        return false;
    default: break;
    }
    Disc disc;   /* a disc image */
    if (!open_disc(p, &disc, err)) {
        if (upper(utf8(p.extension())) != ".ISO")   /* not meant as one: say what is taken */
            *err = "not a file the setup takes: it takes the disc (a decrypted .iso, or the folder holding "
                   "PS3_GAME), packages (.pkg), a mod's movies (.zip) and a mod's folder";
        return false;
    }
    s->kind = SourceKind::Disc;
    s->name = "Drakengard 3 disc image (BLUS31197)";
    s->bytes = disc.total;
    return true;
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
    if (!fs::exists(file, ec)) {
        *err = update_required() ? "the update is not installed" : "the disc is not installed";
        return false;
    }
    if (read_file(file, 256u << 20, self)) return true;
    *err = "cannot read " + utf8(file);
    return false;
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
    /* the progress counts the bytes read: a package's whole file, a disc's or a folder's files */
    auto disc_read = [&] { return plan.disc.psn ? fs::file_size(plan.disc.path, ec) : plan.disc.bytes; };
    auto mod_read = [&](const Source& m) { return m.form == ModForm::Undub ? m.bytes : fs::file_size(m.path, ec); };
    uint64_t total = 0, done = 0;
    if (plan.disc.set()) total += disc_read();
    if (plan.eboot.set()) total += plan.eboot.bytes;
    if (plan.update.set()) total += fs::file_size(plan.update.path, ec);
    for (const Source& d : plan.dlc) total += fs::file_size(d.path, ec);
    for (const Source& m : plan.mods) total += mod_read(m);
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

    const fs::path final_dir = base / "game/disc";
    /* the install's own game/disc chosen as the disc: nothing to copy */
    const bool in_place = plan.disc.set() && !plan.disc.psn && fs::exists(final_dir, ec) &&
                          fs::equivalent(plan.disc.path, final_dir, ec);
    if (plan.disc.set() && !in_place) {
        const fs::path part = stage / "disc", old = stage / "old";
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
        done += disc_read();
        gamedata_layout(final_dir);
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
        forget_patch_mods(base);
        if (!merge_move(part, update_dir(base), err)) return fail(*err);
        done = start + pkg.file_size;
    }

    if (make) {
        if (!progress(done, total, "Writing the game's executable")) return fail("cancelled");
        const fs::path part = stage / "EBOOT.ELF", to = base / eboot_path();
        fs::create_directories(stage, ec);
        fs::create_directories(to.parent_path(), ec);
        const bool wrote = write_file(part, elf.data(), elf.size());
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
        if (d.id == kVoicePackId && !dlc_write_jpv_lists(base / "game/disc", true))
            return fail("the Japanese Voice Pack installed, but its file lists could not be written");
        done = start + pkg.file_size;
    }

    for (const Source& md : plan.mods) {
        const uint64_t start = done;
        const std::string what = "Installing " + md.name;
        if (!install_mod(base, md, stage, [&](uint64_t x) { return progress(start + x, total, what); }, err))
            return fail(*err);
        done = start + mod_read(md);
    }
    fs::remove_all(stage, ec);
    progress(total, total, "Done");
    return true;
}

bool needs_voice_pack(const Plan& plan, const Status& have)
{
    bool undub = false, pack = have.dlc.count(kVoicePackId) != 0;
    for (const Source& m : plan.mods) undub |= m.form == ModForm::Undub;
    for (const Source& d : plan.dlc) pack |= d.id == kVoicePackId;
    return undub && !pack;
}

void plan_add(Plan& plan, const Source& s)
{
    auto replace = [&](std::vector<Source>& v) {
        std::erase_if(v, [&](const Source& o) { return o.id == s.id; });
        v.push_back(s);
    };
    switch (s.kind) {
    case SourceKind::Disc: plan.disc = s; break;
    case SourceKind::Update: plan.update = s; break;
    case SourceKind::Eboot: plan.eboot = s; break;
    case SourceKind::Dlc: replace(plan.dlc); break;
    case SourceKind::Mod: replace(plan.mods); break;
    case SourceKind::Unknown: break;
    }
}

uint64_t made_eboot_bytes() { return update_required() ? 26872424u : 26872552u; }

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
        plan_add(plan, s);
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
