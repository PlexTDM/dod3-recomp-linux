/* Proton on Linux: the builds installed beside Steam, the one the setup
 * starts the game with, and where that choice is kept (proton.conf in the
 * install folder). The setup's Proton page and its Play button both go
 * through here, so the build the page shows is the build that starts. */
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace dod3setup {

namespace fs = std::filesystem;

struct ProtonVersion {
    fs::path path;        /* the build's folder: path/proton starts it */
    fs::path steam_root;  /* the Steam install it came from; empty for DOD3_PROTON */
};

/* The Proton builds found, best first: DOD3_PROTON, then GE-Proton, cachyos
 * and DW-Proton in Steam's compatibilitytools.d, any other tool there with a
 * proton executable, and Steam's own Protons. Empty if there are none. */
std::vector<ProtonVersion> proton_versions();

/* True if `dir` holds an executable `proton`. */
bool proton_usable(const fs::path& dir);

/* The build's name for the player: its folder's name. */
std::string proton_name(const fs::path& dir);

/* Where the choice is kept: <base>/proton.conf (one folder path). */
fs::path proton_config_path(const fs::path& base);

/* The build the setup starts the game with: proton.conf if it still holds a
 * usable build, else DOD3_PROTON, else the first proton_versions() entry.
 * Empty if there is none. */
fs::path proton_selected(const fs::path& base);

/* Was the build chosen by the player (proton.conf), not found automatically? */
bool proton_chosen(const fs::path& base);

/* Keep `dir` for Play; empty removes proton.conf and goes back to finding
 * one. False if `dir` is not a usable build or the file could not be written. */
bool proton_select(const fs::path& base, const fs::path& dir);

}  // namespace dod3setup
