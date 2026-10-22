/* dod3.ini, read and edited in place (ui/ini.h). */
#include "ui/ini.h"
#include "util.h"

#include <cstring>

namespace dod3 {

namespace {

/* The file's lines, without their ends; whether it used CRLF. */
std::vector<std::string> read_lines(const std::filesystem::path& path, bool* crlf)
{
    std::vector<std::string> lines;
    *crlf = false;
    if (FILE* f = open_file(path, "rb")) {
        char buf[1024];
        while (fgets(buf, sizeof buf, f)) {
            std::string l(buf);
            if (l.size() >= 2 && l[l.size() - 2] == '\r') *crlf = true;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            lines.push_back(l);
        }
        fclose(f);
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

}  // namespace

/* As main.cpp reads it: # and ; start a comment, also after a value. */
std::map<std::string, std::string> ini_read(const std::filesystem::path& path)
{
    std::map<std::string, std::string> kv;
    bool crlf;
    for (const std::string& l : read_lines(path, &crlf)) {
        const std::string t = trim(l);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        std::string v = t.substr(eq + 1);
        const size_t c = v.find_first_of("#;");
        if (c != std::string::npos) v.resize(c);
        if (!key.empty()) kv[key] = trim(v);
    }
    return kv;
}

/* KEY = VALUE on the key's line (uncommenting "#KEY = ..."), or appended; a
 * NULL value comments the line out. */
bool ini_write(const std::filesystem::path& path, const std::vector<std::pair<std::string, const char*>>& kv)
{
    bool crlf;    /* written back with the line ends it had */
    std::vector<std::string> lines = read_lines(path, &crlf);
    for (const auto& p : kv) {
        const std::string& key = p.first;
        int hit = -1, commented = -1;
        for (size_t i = 0; i < lines.size(); i++) {
            const char* s = lines[i].c_str();
            while (*s == ' ' || *s == '\t') s++;
            const bool c = *s == '#';
            if (c) {
                s++;
                while (*s == ' ' || *s == '\t') s++;
            }
            if (strncmp(s, key.c_str(), key.size())) continue;
            const char* e = s + key.size();
            while (*e == ' ' || *e == '\t') e++;
            if (*e != '=') continue;
            if (!c) {
                hit = (int)i;
                break;
            }
            if (commented < 0) commented = (int)i;
        }
        const std::string line = p.second ? key + " = " + p.second : "#" + key + " = ";
        if (hit >= 0) {
            if (p.second)
                lines[hit] = line;
            else
                lines[hit] = "#" + lines[hit];
        } else if (p.second) {
            if (commented >= 0)
                lines[commented] = line;
            else
                lines.push_back(line);
        }
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
