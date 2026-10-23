/*
 * The Linux side of the setup: where the game is installed, the command
 * line (--install, --check, --base), and the wizard from src/ui/wizard.cpp
 * in an SDL2 + OpenGL window (src/ui/ui_linux.cpp). The Windows release's
 * dod3.exe can run this install under Proton, or Wine when no Proton is
 * installed; Play starts it when dod3.exe is in the install folder.
 */
#include "install/install.h"
#include "os/linux/proton.h"
#include "ui/setup_shared.h"
#include "ui/ui.h"
#include "util.h"
#include "app_version.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using dod3::utf8;
using namespace dod3setup;

namespace {

fs::path source_root(const fs::path& exe)
{
    std::error_code ec;
    fs::path dir = exe.parent_path();
    for (int i = 0; i < 3 && !dir.empty(); i++) {
        if (fs::exists(dir / "CMakeLists.txt", ec) && fs::exists(dir / "src", ec)) return dir;
        const fs::path up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    return {};
}

fs::path install_base(const fs::path& exe)
{
    if (const char* e = getenv("DOD3_INSTALL_BASE"))
        if (*e) return fs::absolute(fs::path(e)).lexically_normal();
    const fs::path beside = exe.parent_path();
    if (fs::exists(beside / "dod3.exe") || fs::exists(beside / "game")) return beside;
    std::error_code ec;
    if (fs::exists(fs::current_path(ec) / "dod3.exe")) return fs::current_path(ec);
    if (const fs::path src = source_root(exe); !src.empty()) return (src / "game").lexically_normal();
    if (const char* x = getenv("XDG_DATA_HOME"))
        if (*x) return fs::path(x) / "Drakengard 3 Recompiled";
    if (const char* h = getenv("HOME"))
        if (*h) return fs::path(h) / ".local/share/Drakengard 3 Recompiled";
    return fs::current_path(ec);
}

fs::path executable(const char* argv0)
{
    std::error_code ec;
    const fs::path p = fs::absolute(fs::path(argv0 ? argv0 : "dod3-setup"), ec).lexically_normal();
    return ec ? fs::path(argv0 ? argv0 : "dod3-setup") : p;
}

std::string wine_path(const fs::path& p)
{
    std::string out = "Z:";
    for (char c : utf8(p)) out += c == '/' ? '\\' : c;
    return out;
}

fs::path proton_steam_root(const fs::path& proton)
{
    for (const ProtonVersion& v : proton_versions()) {
        if (v.steam_root.empty()) continue;
        std::error_code ec;
        if (v.path == proton || (fs::equivalent(v.path, proton, ec) && !ec)) return v.steam_root;
    }
    return {};
}

bool launch_play(const fs::path& play, const fs::path& base, const fs::path& proton)
{
    if (access(play.c_str(), X_OK) != 0) return false;
    const bool have_proton = proton_usable(proton);
    const std::string chosen = have_proton ? utf8(proton) : std::string();
    const fs::path root = have_proton ? proton_steam_root(proton) : fs::path();
    const std::string root_text = utf8(root);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (chdir(base.c_str()) != 0) _exit(127);
        if (!chosen.empty()) setenv("DOD3_PROTON", chosen.c_str(), 1);
        if (!root.empty()) setenv("STEAM_ROOT", root_text.c_str(), 1);
        execl(play.c_str(), play.c_str(), (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 127);
}

bool launch_proton(const fs::path& base, const fs::path& proton)
{
    if (!proton_usable(proton)) return false;
    fs::path steam_root = proton_steam_root(proton);
    if (steam_root.empty())
        for (const ProtonVersion& v : proton_versions())
            if (!v.steam_root.empty()) {
                steam_root = v.steam_root;
                break;
            }
    if (steam_root.empty()) return false;
    fs::path prefix;
    if (const char* p = getenv("DOD3_PROTON_PREFIX"))
        if (*p) prefix = fs::path(p);
    if (prefix.empty()) {
        const char* home = getenv("HOME");
        if (!home || !*home) return false;
        prefix = fs::path(home) / ".local/share/dod3-recomp/proton";
    }
    std::error_code ec;
    fs::create_directories(prefix, ec);
    if (ec) return false;
    const fs::path exe = proton / "proton";
    const std::string root_text = utf8(steam_root), prefix_text = utf8(prefix);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (chdir(base.c_str()) != 0) _exit(127);
        setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", root_text.c_str(), 1);
        setenv("STEAM_COMPAT_DATA_PATH", prefix_text.c_str(), 1);
        execl(exe.c_str(), exe.c_str(), "run", "dod3.exe", (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 127);
}

bool launch_game(const fs::path& base)
{
    const fs::path exe = base / "dod3.exe";
    if (!fs::exists(exe)) return false;
    const fs::path proton = proton_selected(base);
    if (launch_play(base / "play.sh", base, proton)) return true;
    if (launch_proton(base, proton)) return true;
    for (const char* wine : { "wine", "wine64" }) {
        const pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            if (chdir(base.c_str()) != 0) _exit(127);
            execlp(wine, wine, exe.c_str(), (char*)nullptr);
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return false;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) continue;
        return true;
    }
    return false;
}

void usage(const char* argv0)
{
    const fs::path def = install_base(executable(argv0));
    printf("Drakengard 3 Recompiled %s -- Linux setup\n\n", DOD3_VERSION_STRING);
    printf("usage: %s [--base DIR] [--proton DIR|auto] [--setup]\n", argv0);
    printf("       %s --install [--base DIR] [--proton DIR|auto] [--keys FILE] <disc .iso|folder|pkg> [update .pkg] [DLC .pkg ...]\n",
           argv0);
    printf("       %s --check <file>...\n", argv0);
    printf("       %s --proton-list\n\n", argv0);
    printf("  --base DIR   install here instead of the default\n");
    printf("               (%s)\n", utf8(def).c_str());
    printf("  --install    install without a window\n");
    printf("  --check      say what each file is, installing nothing\n");
    printf("  --keys FILE  the game executable's keys (install/keys.h)\n");
    printf("  --proton DIR   the Proton build Play starts (auto: find one)\n");
    printf("  --proton-list  show the Proton builds found\n");
    printf("  --setup      open the installer even when the game is installed\n");
    printf("\nThe game's files install beside the Windows dod3.exe, in the source\n");
    printf("tree's game folder, or into --base; dod3.exe then runs under Proton\n");
    printf("or Wine. DOD3_INSTALL_BASE sets the default folder.\n");
}

int check_files(const std::vector<fs::path>& files)
{
    int bad = 0;
    for (const fs::path& f : files) {
        dod3setup::Source s;
        std::string err;
        if (dod3setup::identify(f, &s, &err))
            printf("%s: %s (%.2f GB to install)\n", utf8(f).c_str(), s.name.c_str(), (double)s.bytes / (1u << 30));
        else {
            printf("%s: %s\n", utf8(f).c_str(), err.c_str());
            bad = 1;
        }
    }
    return bad;
}

int list_protons(const fs::path& base)
{
    std::vector<ProtonVersion> all = proton_versions();
    const fs::path selected = proton_selected(base);
    if (proton_usable(selected)) {
        const bool listed = std::any_of(all.begin(), all.end(), [&](const ProtonVersion& v) {
            if (v.path == selected) return true;
            std::error_code ec;
            return fs::equivalent(v.path, selected, ec) && !ec;
        });
        if (!listed) all.insert(all.begin(), { selected, {} });
    }
    if (all.empty()) {
        printf("no Proton found (set DOD3_PROTON or install GE-Proton in Steam)\n");
        return 1;
    }
    for (const ProtonVersion& v : all) {
        std::error_code ec;
        const bool is = v.path == selected || (fs::equivalent(v.path, selected, ec) && !ec);
        printf("%c %s (%s)\n", is ? '*' : ' ', proton_name(v.path).c_str(), utf8(v.path).c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path base, proton;
    bool have_base = false, have_proton = false, proton_auto = false, proton_list = false;
    bool install = false, check = false;
    fs::path keys;
    std::vector<fs::path> files;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--base" || a == "-b") {
            if (++i >= argc) {
                fprintf(stderr, "%s needs a folder\n", a.c_str());
                return 2;
            }
            base = fs::path(argv[i]);
            have_base = true;
        } else if (a == "--install") {
            install = true;
        } else if (a == "--check") {
            check = true;
        } else if (a == "--keys") {
            if (++i >= argc) {
                fprintf(stderr, "--keys needs a file\n");
                return 2;
            }
            keys = fs::path(argv[i]);
        } else if (a == "--proton") {
            if (++i >= argc) {
                fprintf(stderr, "--proton needs a folder or auto\n");
                return 2;
            }
            if (!strcmp(argv[i], "auto"))
                proton_auto = true;
            else {
                proton = fs::path(argv[i]);
                have_proton = true;
            }
        } else if (a == "--proton-list") {
            proton_list = true;
        } else if (a == "--setup") {
            /* the default */
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else if (a == "--version") {
            printf("%s\n", DOD3_VERSION_STRING);
            return 0;
        } else if (a == "--remove-mods") {
            if (!install) {
                fprintf(stderr, "--remove-mods goes with --install\n");
                return 2;
            }
            files.emplace_back(a);
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            fprintf(stderr, "unknown option %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        } else {
            files.push_back(fs::path(a));
        }
    }

    if (check) {
        if (files.empty()) {
            fprintf(stderr, "--check needs at least one file\n");
            return 2;
        }
        return check_files(files);
    }

    if (!have_base) base = install_base(executable(argv[0]));
    std::error_code ec;
    base = fs::absolute(base, ec).lexically_normal();
    if (!install) {
        fs::create_directories(base, ec);
        if (ec) {
            fprintf(stderr, "cannot make the install folder %s: %s\n", utf8(base).c_str(), ec.message().c_str());
            return 1;
        }
    }

    if (proton_list) return list_protons(base);
    if (have_proton || proton_auto) {
        if (have_proton && !proton_usable(proton)) {
            fprintf(stderr, "%s is not a Proton build (no executable proton in it)\n", utf8(proton).c_str());
            return 2;
        }
        fs::create_directories(base, ec);
        if (ec || !proton_select(base, have_proton ? proton : fs::path())) {
            fprintf(stderr, "cannot save the Proton choice in %s\n", utf8(base).c_str());
            return 1;
        }
    }

    if (install) {
        if (files.empty()) {
            fprintf(stderr, "--install needs the game's files (or --remove-mods)\n");
            return 2;
        }
        return dod3setup::install_files_cli(base, files, keys);
    }

    printf("Drakengard 3 Recompiled %s -- Linux setup\n", DOD3_VERSION_STRING);
    printf("install folder: %s\n", utf8(base).c_str());
    const fs::path chosen = proton_selected(base);
    if (proton_usable(chosen))
        printf("proton: %s (%s)\n", proton_name(chosen).c_str(), utf8(chosen).c_str());
    else
        printf("proton: none found; Play falls back to Wine\n");
    fflush(stdout);

    const int r = dod3setup::run_installer(base);
    if (r < 0) {
        fprintf(stderr,
                "the setup window could not open (SDL2 + OpenGL).\n"
                "Install from a terminal instead:\n"
                "  %s --install --base %s <disc .iso or folder> <update .pkg> [DLC .pkg ...]\n",
                argv[0], utf8(base).c_str());
        return 1;
    }

    if (!dod3setup::installed(base).ready()) return r == dod3setup::kQuit ? 0 : 1;

    printf("installed into %s\n", utf8(base).c_str());
    if (r == dod3setup::kPlay) {
        if (launch_game(base))
            printf("starting dod3.exe\n");
        else
            printf("no dod3.exe in the install folder (or Wine is not installed).\n"
                   "Unzip the Windows release there and run:\n"
                   "  cd '%s' && wine dod3.exe\n"
                   "or set DOD3_INSTALL_BASE='%s' when running dod3.exe under Wine.\n",
                   utf8(base).c_str(), wine_path(base).c_str());
    }
    return 0;
}
