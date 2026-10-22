/* dod3.ini as the player sees it: read into KEY -> VALUE, and edited in
 * place -- a key's line rewritten (or its "#KEY = ..." line uncommented),
 * else appended, the comments and the line ends kept. The in-game Settings
 * pages (src/ui/settings_menu.cpp) and the launcher (src/ui/launcher.cpp)
 * both write through here; main.cpp reads the file into the environment at
 * start-up. */
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dod3 {

/* The keys set in the file (the lines not commented out). Empty if there is
 * no file. */
std::map<std::string, std::string> ini_read(const std::filesystem::path& path);

/* Each KEY = VALUE written; a NULL value comments the key's line out. False
 * if the file could not be written. */
bool ini_write(const std::filesystem::path& path, const std::vector<std::pair<std::string, const char*>>& kv);

}  // namespace dod3
