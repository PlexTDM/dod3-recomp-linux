/* Proton discovery and its saved choice (os/linux/proton.h). */
#include "os/linux/proton.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <unistd.h>

namespace dod3setup {

namespace {

std::string lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

/* A version's name compared the way sort -V does, so GE-Proton9-27 is above
 * GE-Proton9-2. */
int natural_compare(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ia = i, jb = j;
            while (ia < a.size() && a[ia] == '0') ia++;
            while (jb < b.size() && b[jb] == '0') jb++;
            size_t ea = ia, eb = jb;
            while (ea < a.size() && std::isdigit((unsigned char)a[ea])) ea++;
            while (eb < b.size() && std::isdigit((unsigned char)b[eb])) eb++;
            if (ea - ia != eb - jb) return ea - ia < eb - jb ? -1 : 1;
            const int c = a.compare(ia, ea - ia, b, jb, eb - jb);
            if (c) return c < 0 ? -1 : 1;
            i = ea;
            j = eb;
            continue;
        }
        const unsigned char la = (unsigned char)std::tolower(ca), lb = (unsigned char)std::tolower(cb);
        if (la != lb) return la < lb ? -1 : 1;
        i++;
        j++;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

bool same_path(const fs::path& a, const fs::path& b)
{
    if (a == b) return true;
    std::error_code ec;
    const fs::path ca = fs::weakly_canonical(a, ec), cb = fs::weakly_canonical(b, ec);
    return !ec && ca == cb;
}

std::vector<fs::path> steam_roots()
{
    std::vector<fs::path> roots;
    auto add = [&](const fs::path& p) {
        if (p.empty()) return;
        for (const fs::path& r : roots)
            if (same_path(r, p)) return;
        roots.push_back(p);
    };
    if (const char* root = getenv("STEAM_ROOT"))
        if (*root) add(fs::path(root));
    if (const char* home = getenv("HOME")) {
        if (*home) {
            const fs::path h(home);
            add(h / ".local/share/Steam");
            add(h / ".steam/steam");
            add(h / ".var/app/com.valvesoftware.Steam/data/Steam");
        }
    }
    return roots;
}

std::vector<fs::path> entries(const fs::path& dir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec))
        out.push_back(it->path());
    return out;
}

/* What a Proton build is called for the player: its folder's name. */
std::string dir_name(const fs::path& p)
{
    const std::string n = p.filename().string();
    return n.empty() ? p.string() : n;
}

int group_of(const std::string& name)
{
    const std::string n = lower(name);
    if (starts_with(n, "ge-proton")) return 1;
    if (starts_with(n, "cachyos")) return 2;
    if (starts_with(n, "dw-proton")) return 3;
    return 4;
}

/* Steam's own builds: numbered Protons first, newest first, then Experimental
 * and Hotfix (play.sh's order). */
int official_rank(const std::string& name)
{
    const std::string n = lower(name);
    if (starts_with(n, "proton ") && n.size() > 7 && std::isdigit((unsigned char)n[7])) return 0;
    if (n.find("experimental") != std::string::npos) return 1;
    if (n.find("hotfix") != std::string::npos) return 2;
    return 3;
}

}  // namespace

bool proton_usable(const fs::path& dir)
{
    if (dir.empty()) return false;
    std::error_code ec;
    const fs::path exe = dir / "proton";
    return fs::is_regular_file(exe, ec) && access(exe.c_str(), X_OK) == 0;
}

std::string proton_name(const fs::path& dir) { return dir_name(dir); }

std::vector<ProtonVersion> proton_versions()
{
    struct Found {
        int group;
        ProtonVersion v;
    };
    std::vector<Found> found;
    std::map<std::string, size_t> seen;
    auto add = [&](const fs::path& path, const fs::path& root, int group) {
        std::error_code ec;
        const fs::path clean = fs::absolute(path, ec).lexically_normal();
        if (ec || !proton_usable(clean)) return;
        fs::path steam_root = root;
        if (steam_root.empty()) {
            const fs::path parent = clean.parent_path();
            if (parent.filename() == "compatibilitytools.d")
                steam_root = parent.parent_path();
            else if (parent.filename() == "common" && parent.parent_path().filename() == "steamapps")
                steam_root = parent.parent_path().parent_path();
        }
        const fs::path canonical = fs::weakly_canonical(clean, ec);
        const std::string key = ec ? clean.string() : canonical.string();
        const auto it = seen.find(key);
        if (it != seen.end()) {
            Found& f = found[it->second];
            if (f.v.steam_root.empty() && !steam_root.empty()) f.v.steam_root = steam_root;
            if (group == 0) f.group = 0;
            return;
        }
        seen.emplace(key, found.size());
        found.push_back({ group, { clean, steam_root } });
    };

    if (const char* custom = getenv("DOD3_PROTON"))
        if (*custom) add(fs::path(custom), {}, 0);

    const std::vector<fs::path> roots = steam_roots();
    for (const fs::path& root : roots) {
        for (const fs::path& p : entries(root / "compatibilitytools.d"))
            add(p, root, group_of(dir_name(p)));
        for (const fs::path& p : entries(root / "steamapps/common"))
            if (starts_with(lower(dir_name(p)), "proton")) add(p, root, 5);
    }

    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.group != b.group) return a.group < b.group;
        const std::string an = dir_name(a.v.path), bn = dir_name(b.v.path);
        if (a.group == 5) {
            const int ar = official_rank(an), br = official_rank(bn);
            if (ar != br) return ar < br;
        }
        return natural_compare(an, bn) > 0;
    });
    std::vector<ProtonVersion> out;
    out.reserve(found.size());
    for (const Found& f : found) out.push_back(f.v);
    return out;
}

fs::path proton_config_path(const fs::path& base) { return base / "proton.conf"; }

namespace {

fs::path saved_proton(const fs::path& base)
{
    std::ifstream in(proton_config_path(base));
    std::string line;
    while (std::getline(in, line)) {
        const size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos || line[a] == '#') continue;
        const size_t b = line.find_last_not_of(" \t\r");
        fs::path p = dod3::path_from_utf8(line.substr(a, b - a + 1));
        if (p.is_relative()) p = base / p;
        return p.lexically_normal();
    }
    return {};
}

}  // namespace

bool proton_chosen(const fs::path& base) { return proton_usable(saved_proton(base)); }

fs::path proton_selected(const fs::path& base)
{
    if (const fs::path saved = saved_proton(base); proton_usable(saved)) return saved;
    if (const char* custom = getenv("DOD3_PROTON")) {
        if (*custom) {
            const fs::path p(custom);
            if (proton_usable(p)) {
                std::error_code ec;
                const fs::path clean = fs::absolute(p, ec).lexically_normal();
                return ec ? p : clean;
            }
        }
    }
    const std::vector<ProtonVersion> all = proton_versions();
    return all.empty() ? fs::path() : all.front().path;
}

bool proton_select(const fs::path& base, const fs::path& dir)
{
    const fs::path file = proton_config_path(base);
    std::error_code ec;
    if (dir.empty()) {
        fs::remove(file, ec);
        return !ec || !fs::exists(file, ec);
    }
    if (!proton_usable(dir)) return false;
    const fs::path clean = fs::absolute(dir, ec).lexically_normal();
    if (ec) return false;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const std::string text = dod3::utf8(clean);
    out.write(text.data(), (std::streamsize)text.size());
    out.put('\n');
    return (bool)out;
}

}  // namespace dod3setup
