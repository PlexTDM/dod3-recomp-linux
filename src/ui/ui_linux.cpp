/*
 * The installer's window on Linux: SDL2 + OpenGL 3 under Dear ImGui's SDL2
 * and OpenGL3 backends, and the desktop's file pickers through zenity,
 * kdialog or yad. See ui/ui.h; this follows imgui's
 * examples/example_sdl2_opengl3, with these differences:
 *   - closing the window is the wizard's Quit (the game's own window and
 *     loop come after the setup in the same process);
 *   - the window is fixed-size, centred, and follows the display's scale;
 *   - DOD3_SETUP_GRAB=<prefix>: <prefix>.req asks for the next frame as
 *     <prefix>.ppm, the protocol src/ui/wizard.cpp uses.
 */
#include "ui/ui.h"
#include "util.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

namespace dod3setup {

namespace {

SDL_Window* g_window = nullptr;
SDL_GLContext g_gl = nullptr;
bool g_closed = false;
float g_dpi = 1.0f;
float g_dpi_applied = 1.0f;
const char* g_glsl = "#version 130";

const char* grab_prefix()
{
    static const char* p = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        p = getenv("DOD3_SETUP_GRAB");
        if (p && !*p) p = nullptr;
    }
    return p;
}

void grab(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    const std::string tmp = std::string(grab_prefix()) + ".ppm.tmp";
    const std::string out = std::string(grab_prefix()) + ".ppm";
    const std::string req = std::string(grab_prefix()) + ".req";
    std::error_code ec;
    if (!std::filesystem::exists(req, ec)) return;

    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* row = &px[(size_t)y * w * 4];
        for (int x = 0; x < w; x++) fwrite(&row[(size_t)x * 4], 1, 3, f);
    }
    fclose(f);
    std::filesystem::rename(tmp, out, ec);
    std::filesystem::remove(req, ec);
}

void destroy_window()
{
    if (g_gl) {
        SDL_GL_DeleteContext(g_gl);
        g_gl = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
}

bool make_window(const char* title, int width, int height)
{
    const float scale = std::clamp(ImGui_ImplSDL2_GetContentScaleForDisplay(0), 1.0f, 4.0f);
    int w = (int)(width * scale), h = (int)(height * scale);
    SDL_Rect bounds = {};
    if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
        const float room_w = (float)bounds.w * 0.94f, room_h = (float)bounds.h * 0.90f;
        if (w > room_w || h > room_h) {
            const float k = (std::min)(room_w / w, room_h / h);
            w = (int)(w * k);
            h = (int)(h * k);
        }
    }
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    g_window = SDL_CreateWindow(title ? title : "Drakengard 3 Recompiled", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!g_window) {
        fprintf(stderr, "[setup] SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    g_dpi = scale;
    return true;
}

void split_lines(const std::string& s, std::vector<std::string>* out)
{
    std::string cur;
    for (char c : s) {
        if (c == '\n' || c == '|') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            if (!cur.empty()) out->push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
    if (!cur.empty()) out->push_back(cur);
}

std::string quote(const std::string& s)
{
    std::string q = "'";
    for (char c : s) {
        if (c == '\'')
            q += "'\\''";
        else
            q += c;
    }
    q += "'";
    return q;
}

bool tool_exists(const char* name)
{
    const char* path = getenv("PATH");
    if (!path) return false;
    const std::string all(path);
    for (size_t at = 0; at <= all.size();) {
        const size_t e = all.find(':', at);
        const std::string dir = all.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (!dir.empty()) {
            const std::string p = dir + "/" + name;
            if (access(p.c_str(), X_OK) == 0) return true;
        }
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return false;
}

std::vector<std::string> run_dialog(const std::string& cmd, bool* ok)
{
    std::vector<std::string> out;
    *ok = false;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    std::string all;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, p)) > 0;) all.append(buf, n);
    const int st = pclose(p);
    if (st == -1 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) return out;
    *ok = true;
    split_lines(all, &out);
    return out;
}

std::string globs(const std::string& spec)
{
    std::string out;
    for (char c : spec) out += c == ';' ? ' ' : c;
    return out;
}

std::vector<std::string> pick_files_impl(const char* title,
                                         const std::vector<std::pair<std::string, std::string>>& filters)
{
    std::vector<std::string> out;
    const std::string t = title ? title : "Open";
    bool have_zenity = tool_exists("zenity");
    bool have_kdialog = !have_zenity && tool_exists("kdialog");
    bool have_yad = !have_zenity && !have_kdialog && tool_exists("yad");
    if (!have_zenity && !have_kdialog && !have_yad) {
        fprintf(stderr, "[setup] no file dialog found (install zenity, kdialog or yad)\n");
        return out;
    }
    bool ok = false;
    if (have_zenity) {
        std::string cmd = "zenity --file-selection --multiple --separator='|' --title=" + quote(t);
        for (const auto& f : filters)
            cmd += " --file-filter=" + quote(f.first + " | " + globs(f.second));
        out = run_dialog(cmd, &ok);
    } else if (have_kdialog) {
        std::string cmd = "kdialog --title " + quote(t) + " --getopenfilename --multiple --separate-output";
        const char* home = getenv("HOME");
        cmd += " " + quote(home && *home ? home : ".");
        for (const auto& f : filters) cmd += " " + quote(f.first + " (" + globs(f.second) + ")");
        out = run_dialog(cmd, &ok);
    } else {
        std::string cmd = "yad --file --multiple --separator='|' --title=" + quote(t);
        for (const auto& f : filters) cmd += " --file-filter=" + quote(f.first + " | " + globs(f.second));
        out = run_dialog(cmd, &ok);
    }
    (void)ok;
    return out;
}

std::string pick_folder_impl(const char* title)
{
    const std::string t = title ? title : "Choose a folder";
    bool ok = false;
    std::vector<std::string> out;
    if (tool_exists("zenity")) {
        out = run_dialog("zenity --file-selection --directory --title=" + quote(t), &ok);
    } else if (tool_exists("kdialog")) {
        const char* home = getenv("HOME");
        out = run_dialog("kdialog --title " + quote(t) + " --getexistingdirectory " +
                             quote(home && *home ? home : "."),
                         &ok);
    } else if (tool_exists("yad")) {
        out = run_dialog("yad --file --directory --title=" + quote(t), &ok);
    } else {
        fprintf(stderr, "[setup] no folder dialog found (install zenity, kdialog or yad)\n");
    }
    return (!out.empty() && ok) ? out.front() : std::string();
}

}  // namespace

bool ui_init(const char* title, int width, int height, const std::string& font_path, float font_px)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[setup] SDL_Init: %s\n", SDL_GetError());
        return false;
    }
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if (!make_window(title, width, height)) {
        SDL_Quit();
        return false;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        fprintf(stderr, "[setup] OpenGL 3.0 context: %s; trying 2.1\n", SDL_GetError());
        destroy_window();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        if (!make_window(title, width, height)) {
            SDL_Quit();
            return false;
        }
        g_gl = SDL_GL_CreateContext(g_window);
        g_glsl = "#version 120";
    }
    if (!g_gl) {
        fprintf(stderr, "[setup] SDL_GL_CreateContext: %s\n", SDL_GetError());
        destroy_window();
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(g_window, g_gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    if (!ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl) || !ImGui_ImplOpenGL3_Init(g_glsl)) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        destroy_window();
        SDL_Quit();
        return false;
    }
    if (!font_path.empty() && !io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_px))
        fprintf(stderr, "[setup] could not load the font %s\n", font_path.c_str());
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    ImGui::GetStyle().FontSizeBase = font_px;

    g_closed = false;
    g_dpi = std::clamp(ImGui_ImplSDL2_GetContentScaleForDisplay(0), 1.0f, 4.0f);
    g_dpi_applied = 1.0f;
    SDL_ShowWindow(g_window);
    return true;
}

bool ui_frame_begin()
{
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) g_closed = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) g_closed = true;
        }
        if (g_closed) return false;
        if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(16);
            continue;
        }
        break;
    }
    const int display = SDL_GetWindowDisplayIndex(g_window);
    const float dpi = std::clamp(ImGui_ImplSDL2_GetContentScaleForDisplay(display), 1.0f, 4.0f);
    if (dpi != g_dpi) g_dpi = dpi;
    if (g_dpi != g_dpi_applied) {
        ImGuiStyle& st = ImGui::GetStyle();
        st.ScaleAllSizes(g_dpi / g_dpi_applied);
        st.FontScaleDpi = g_dpi;
        g_dpi_applied = g_dpi;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    return true;
}

void ui_frame_end()
{
    ImGui::Render();
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(g_window, &w, &h);
    glViewport(0, 0, w, h);
    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (grab_prefix()) grab(w, h);
    SDL_GL_SwapWindow(g_window);
}

void ui_shutdown()
{
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    destroy_window();
    SDL_Quit();
    g_closed = false;
}

std::vector<std::filesystem::path> ui_pick_files(const char* title,
                                                 const std::vector<std::pair<std::string, std::string>>& filters)
{
    std::vector<std::filesystem::path> out;
    for (const std::string& p : pick_files_impl(title, filters)) out.emplace_back(p);
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    return out;
}

std::filesystem::path ui_pick_folder(const char* title)
{
    const std::string p = pick_folder_impl(title);
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    return p.empty() ? std::filesystem::path() : std::filesystem::path(p);
}

}  // namespace dod3setup
