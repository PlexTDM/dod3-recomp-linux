/* dod3.ini, read and edited in place (ui/ini.h). */
#include "ui/ini.h"
#include "util.h"

#include <cstdio>

namespace dod3 {

namespace {

/* The file's lines, without their ends; whether it used CRLF. */
std::vector<std::string> read_lines(const std::filesystem::path& path, bool* crlf = nullptr)
{
    std::vector<std::string> lines;
    std::string all;
    if (FILE* f = open_file(path, "rb")) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) all.append(buf, n);
        fclose(f);
    }
    if (crlf) *crlf = all.find("\r\n") != std::string::npos;
    for (size_t at = 0; at < all.size();) {
        size_t e = all.find('\n', at);
        if (e == std::string::npos) e = all.size();
        std::string l = all.substr(at, e - at);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        at = e + 1;
    }
    return lines;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

/* Does `line` set `key` -- "KEY = ...", or commented out, "#KEY = ..."? */
bool sets_key(const std::string& line, const std::string& key, bool* commented)
{
    std::string t = trim(line);
    *commented = !t.empty() && t[0] == '#';
    if (*commented) t = trim(t.substr(1));
    if (t.compare(0, key.size(), key) != 0) return false;
    const std::string rest = trim(t.substr(key.size()));
    return !rest.empty() && rest[0] == '=';
}

}  // namespace

/* # and ; start a comment, also after a value. A key set twice keeps its
 * first value, as the game takes it (main.cpp) and ini_write edits it. */
std::map<std::string, std::string> ini_read(const std::filesystem::path& path)
{
    std::map<std::string, std::string> kv;
    for (const std::string& l : read_lines(path)) {
        const std::string t = trim(l);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        std::string v = t.substr(eq + 1);
        const size_t c = v.find_first_of("#;");
        if (c != std::string::npos) v.resize(c);
        if (!key.empty()) kv.emplace(key, trim(v));
    }
    return kv;
}

/* KEY = VALUE on the key's line (uncommenting "#KEY = ..."), or appended; a
 * NULL value comments the line out. */
bool ini_write(const std::filesystem::path& path, const std::vector<std::pair<std::string, const char*>>& kv)
{
    bool crlf;    /* written back with the line ends it had */
    std::vector<std::string> lines = read_lines(path, &crlf);
    for (const auto& [key, value] : kv) {
        int live_at = -1, commented_at = -1;
        for (size_t i = 0; i < lines.size() && live_at < 0; i++) {
            bool commented;
            if (!sets_key(lines[i], key, &commented)) continue;
            if (!commented)
                live_at = (int)i;
            else if (commented_at < 0)
                commented_at = (int)i;
        }
        if (!value) {   /* back to the default: the line commented out */
            if (live_at >= 0) lines[live_at] = "#" + lines[live_at];
            continue;
        }
        const std::string line = key + " = " + value;
        const int at = live_at >= 0 ? live_at : commented_at;
        if (at >= 0)
            lines[at] = line;
        else
            lines.push_back(line);
    }
    FILE* f = open_file(path, "wb");
    if (!f) {
        fprintf(stderr, "[settings] could not write %s\n", utf8(path).c_str());
        return false;
    }
    for (const std::string& l : lines) fprintf(f, "%s%s", l.c_str(), crlf ? "\r\n" : "\n");
    fclose(f);
    fprintf(stderr, "[settings] saved %s\n", utf8(path).c_str());
    return true;
}

}  // namespace dod3
