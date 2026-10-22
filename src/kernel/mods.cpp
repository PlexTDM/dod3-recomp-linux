/* Files served from beside the game's own, through the runtime's path hook
 * (ps3_vfs_title_hook: every host path the title's file calls resolve to
 * passes through it). Two kinds, both installed by the setup
 * (src/install/install.h):
 *
 *  - A mod's files in game/mods, each at the path under the game tree
 *    (game/disc) it stands in for -- a translation's title screen in the
 *    game-data install, its movies on the disc, its DLC text. The player's
 *    own files stay as they are; the mod is in game/mods alone.
 *
 *  - The DLC chapters' Japanese voices (DLC_JPV/UNDUB, src/kernel/dlc.cpp).
 *    The title finds a voice-pack file by its name alone, and a few of the
 *    undub's names are in more than one chapter, or in the main game too:
 *    each chapter's dragon (ANIM_DLC_DR00_SF), One's BS070. Those are listed
 *    as /DLC_JPV/UNDUB/CHAPTER/<name> and resolved here to the copy of the
 *    chapter being played -- the one whose map was read last since the book
 *    or the title -- or, outside the chapters, to the main game's own
 *    package. (The undub changed each chapter's dragon for that chapter's
 *    voices, where the title itself reads one chapter's in all of them.)
 *    The undub's movies sit in UNDUB/MOVIES and are served where the title
 *    looks for the voice pack's (DLC_JPV/SQEX03GAME/MOVIES). Where a mod in
 *    game/mods has its own copy of a DLC chapter's package the undub has
 *    too -- a translation's text -- the mod's is read in Japanese as well:
 *    the undub's is English text retimed for the Japanese voices.
 *
 * Bridge 18 (src/ui/settings_menu.cpp) asks dod3_dlc_jpvoice(): with the
 * undub installed, the DLC chapters keep the Japanese voices instead of the
 * 1.01 update's switch to English there (tools/menu_patch.py). */
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "util.h"

extern "C" void (*ps3_vfs_title_hook)(char* path, size_t cap);   /* runtime/syscalls/sys_fs.c */

namespace fs = std::filesystem;

namespace {

std::string s_root;                         /* the game tree (PS3_VFS_ROOT): '/', upper case */
std::string s_mods;                         /* game/mods, as the host spells it */
std::unordered_set<std::string> s_tree;     /* paths under the game tree that game/mods has: "/GAME/..." */
std::string s_undub;                        /* DLC_JPV/UNDUB */
std::unordered_set<std::string> s_movies;   /* the undub's movies' names */
std::unordered_set<std::string> s_chapter[6];   /* each chapter's own copies' names */
std::unordered_set<std::string> s_base;     /* of those names, the main game's */
std::unordered_map<std::string, std::string> s_mod_dlc;   /* a DLC package's name -> its path in game/mods */
std::atomic<int> s_playing{ -1 };           /* the DLC chapter whose map was read last, else -1 */
bool s_jpvoice = false;
std::atomic<int> s_said{ 0 };

std::string upper_slash(const std::string& s)
{
    std::string u = s;
    for (char& c : u) c = c == '\\' ? '/' : (char)toupper((unsigned char)c);
    return u;
}

bool ends_with(const char* s, size_t n, const char* tail)
{
    const size_t t = strlen(tail);
    return n >= t && memcmp(s + n - t, tail, t) == 0;
}

/* DOD3_MODS_LOG=1: every path served, not only the first 48 */
void serve(char* path, size_t cap, const std::string& to, const char* rel)
{
    static const bool all = getenv("DOD3_MODS_LOG") != nullptr;
    if (all || s_said.fetch_add(1) < 48) fprintf(stderr, "[mods] %s -> %s\n", rel, to.c_str());
    snprintf(path, cap, "%s", to.c_str());
}

void hook(char* path, size_t cap)
{
    const size_t n = strlen(path), rn = s_root.size();
    if (n <= rn || n >= 1024) return;
    char up[1024];
    for (size_t i = 0; i < n; i++) {
        const char c = path[i];
        up[i] = c == '\\' ? '/' : (char)toupper((unsigned char)c);
    }
    up[n] = 0;
    if (memcmp(up, s_root.data(), rn) != 0 || up[rn] != '/') return;
    const char* rel = up + rn;   /* "/GAME/..." */
    const size_t len = n - rn;

    if (s_jpvoice) {
        /* the chapter being played: the one whose map was read last since the
         * book or the title. Some chapters stream main-game maps too (Two's,
         * Three's), which leave it as it is. */
        if (ends_with(rel, len, "_MAP.XXX")) {
            const char* d = strstr(rel, "/USRDIR/DLC/ADDSCENARIO0000");
            if (d && d[27] >= '0' && d[27] <= '5') s_playing = d[27] - '0';
        } else if (ends_with(rel, len, "/COOKEDPS3/BOOK.XXX") || ends_with(rel, len, "/COOKEDPS3/TITLE.XXX")) {
            s_playing = -1;
        }
        static const char kCooked[] = "/GAME/NPUB31251/USRDIR/DLC_JPV/UNDUB/COOKEDPS3/";
        if (!s_mod_dlc.empty() && !strncmp(rel, kCooked, sizeof kCooked - 1)) {
            const auto m = s_mod_dlc.find(rel + sizeof kCooked - 1);
            if (m != s_mod_dlc.end()) {
                serve(path, cap, s_mods + m->second, rel);
                return;
            }
        }
        static const char kChapter[] = "/GAME/NPUB31251/USRDIR/DLC_JPV/UNDUB/CHAPTER/";
        static const char kMovies[] = "/GAME/NPUB31251/USRDIR/DLC_JPV/SQEX03GAME/MOVIES/";
        if (!strncmp(rel, kChapter, sizeof kChapter - 1)) {
            const std::string name = rel + sizeof kChapter - 1;
            const int c = s_playing;
            if (c >= 0 && s_chapter[c].count(name)) {
                serve(path, cap, s_undub + "/ADDSCENARIO0000" + std::to_string(c) + "/" + name, rel);
            } else if (s_base.count(name)) {
                const std::string root(path, rn);
                serve(path, cap, root + "/PS3_GAME/USRDIR/SQEX03GAME/COOKEDPS3/" + name, rel);
            } else {
                for (int i = 0; i < 6; i++)
                    if (s_chapter[i].count(name)) {
                        serve(path, cap, s_undub + "/ADDSCENARIO0000" + std::to_string(i) + "/" + name, rel);
                        break;
                    }
            }
            return;
        }
        if (!strncmp(rel, kMovies, sizeof kMovies - 1) && s_movies.count(rel + sizeof kMovies - 1)) {
            serve(path, cap, s_undub + "/MOVIES/" + std::string(rel + sizeof kMovies - 1), rel);
            return;
        }
    }
    if (!s_tree.empty() && s_tree.count(rel)) serve(path, cap, s_mods + std::string(path + rn), rel);
}

}  // namespace

/* With the DLC chapters' Japanese voices installed (and the voice pack they
 * join), bridge 18 keeps the chapters in Japanese. */
bool dod3_dlc_jpvoice() { return s_jpvoice; }

/* At boot: what game/mods and DLC_JPV/UNDUB hold, and the hook if either has
 * anything. */
void dod3_mods_prepare()
{
    const char* r = getenv("PS3_VFS_ROOT");
    if (!r || !*r) return;
    std::error_code ec;
    const fs::path root = fs::u8path(r);
    s_root = upper_slash(r);
    while (!s_root.empty() && s_root.back() == '/') s_root.pop_back();

    const fs::path mods = root.parent_path() / "mods";
    s_mods = dod3::utf8(mods);
    for (fs::recursive_directory_iterator it(mods, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = dod3::utf8(fs::relative(it->path(), mods, ec));
        if (rel == "installed.txt") continue;
        const std::string key = "/" + upper_slash(rel);
        s_tree.insert(key);
        if (key.rfind("/GAME/NPUB31251/USRDIR/DLC/", 0) == 0)
            s_mod_dlc[upper_slash(dod3::utf8(it->path().filename()))] = "/" + rel;
    }

    const fs::path jpv = root / "game/NPUB31251/USRDIR/DLC_JPV", undub = jpv / "UNDUB";
    if (fs::exists(undub / "UNDUB.TXT", ec) && fs::is_directory(jpv / "SQEX03GAME", ec)) {
        s_undub = dod3::utf8(undub);
        for (fs::directory_iterator it(undub / "MOVIES", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec)) s_movies.insert(upper_slash(dod3::utf8(it->path().filename())));
        const fs::path cooked = root / "PS3_GAME/USRDIR/SQEX03GAME/COOKEDPS3";
        for (int i = 0; i < 6; i++)
            for (fs::directory_iterator it(undub / ("ADDSCENARIO0000" + std::to_string(i)), ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string name = upper_slash(dod3::utf8(it->path().filename()));
                s_chapter[i].insert(name);
                if (fs::exists(cooked / it->path().filename(), ec)) s_base.insert(name);
            }
        s_jpvoice = true;
        fprintf(stderr, "[mods] the DLC chapters' Japanese voices: %zu movies, %zu packages served by chapter\n",
                s_movies.size(), s_chapter[0].size() + s_chapter[1].size() + s_chapter[2].size() +
                                     s_chapter[3].size() + s_chapter[4].size() + s_chapter[5].size());
    }
    if (!s_tree.empty()) fprintf(stderr, "[mods] %zu files from %s\n", s_tree.size(), s_mods.c_str());
    if (s_jpvoice || !s_tree.empty()) ps3_vfs_title_hook = hook;
}
