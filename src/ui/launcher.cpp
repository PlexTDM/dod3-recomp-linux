/* The launcher: what the player sees at each start once the game is
 * installed (ui/ui.h, run_launcher). The game's own key art (PS3_GAME/PIC1.PNG
 * from the player's install; nothing of the game's ships with the port)
 * fills the window, the title and a menu on its left -- Play, Mods, DLC,
 * Settings, Quit -- and each section opens as a panel over the art:
 *
 *   Mods      the fan-made additions the setup knows (the Spanish translation,
 *             the DLC Undub) and any other installed: what is in, Remove, and
 *             Add Files / Add Folder for more (install/install.h does the work)
 *   DLC       the 20 packs, which are in, and Add DLC Packages
 *   Settings  dod3.ini: the in-game Settings pages' rows (src/ui/settings_menu.cpp,
 *             one list for both), a few that are only in the file (window size,
 *             mouse camera, camera speed, this launcher) and the keyboard's keys.
 *             A change is saved at once, the file's comments kept (ui/ini.h).
 *
 * Everything is drawn in units of the window's height (720 = 1), so the page
 * is the same at any size and on a Retina display; the type is Inter 4.1,
 * built in (third_party/fonts/inter). Keyboard, mouse and controller all work
 * (the backends feed ImGui's navigation).
 *
 * Tests: DOD3_LAUNCHER_OPEN=mods|dlc|settings[:<tab>] opens a panel at start,
 * DOD3_LAUNCHER_ADD='<file>|<file>' adds files as Add Files would, and
 * DOD3_SETUP_GRAB=<prefix> saves the window as <prefix>_0.ppm once it has
 * settled (the platform's grab); DOD3_LAUNCHER_AUTO=quit then closes it,
 * =install presses Install (or Remove) and closes it when that is done.
 * DOD3_LAUNCHER_REMOVE=spanish|undub starts removing one; DOD3_LAUNCHER_KEYS=
 * 'Down,Down,Space,...' presses those keys, one every 12 frames (a key a
 * player presses, as ImGui gets it from the platform). */
#include "ui/ui.h"
#include "ui/ini.h"
#include "install/install.h"
#include "patches/sysset.h"   /* Dod3RowSpec, dod3_setting_rows */
#include "util.h"
#include "app_version.h"      /* generated: the port's version label */
#include "launcher_fonts.h"   /* generated: kFont_* */

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"   /* RegisterUserTexture */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <thread>

namespace dod3setup {

bool png_rgba(const std::vector<uint8_t>& file, std::vector<uint8_t>* rgba, int* w, int* h);   /* ui/png.cpp */

namespace {

using dod3::utf8;

/* ---- look ------------------------------------------------------------------ */

const ImU32 kAccent = IM_COL32(196, 30, 45, 255);
const ImU32 kAccentHi = IM_COL32(222, 50, 64, 255);
const ImU32 kText = IM_COL32(242, 240, 236, 255);
const ImU32 kDim = IM_COL32(170, 166, 172, 255);
const ImU32 kFaint = IM_COL32(118, 116, 124, 255);
const ImU32 kOk = IM_COL32(104, 200, 128, 255);
const ImU32 kWarn = IM_COL32(236, 176, 78, 255);
const ImU32 kErr = IM_COL32(236, 92, 96, 255);
const ImU32 kPanel = IM_COL32(13, 13, 17, 232);
const ImU32 kCard = IM_COL32(255, 255, 255, 9);
const ImU32 kLine = IM_COL32(255, 255, 255, 20);

ImU32 alpha(ImU32 c, float a)
{
    const int na = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)na << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(x, y, std::clamp(t, 0.0f, 1.0f)));
}

float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

std::string gb(uint64_t b)
{
    char s[32];
    if (b >= (1ull << 30))
        snprintf(s, sizeof s, "%.1f GB", b / 1073741824.0);
    else
        snprintf(s, sizeof s, "%.0f MB", b / 1048576.0 + 0.5);
    return s;
}

struct Fonts {
    ImFont *regular = nullptr, *medium = nullptr, *semibold = nullptr, *display = nullptr;
};

/* ---- the settings ------------------------------------------------------------ */

enum Tab { kDisplay, kGraphics, kControls, kSystem, kTabs };
const char* const kTabNames[kTabs] = { "Display", "Graphics", "Controls", "System" };

/* The rows only the file has (the in-game pages have the rest). */
void extra_rows(std::vector<Dod3RowSpec>& rows)
{
    rows.push_back({ "RSX_WINDOW",
                     "Window Size",
                     "The window's size when the display mode is Windowed.",
                     { { "1280x720", "1280x720" },
                       { "1600x900", "1600x900" },
                       { "1920x1080", "1920x1080" },
                       { "2560x1440", "2560x1440" },
                       { "3840x2160", "3840x2160" } },
                     0,
                     false,
                     nullptr,
                     0 });
    rows.push_back({ "DOD3_MOUSE",
                     "Mouse Camera",
                     "The mouse turns the camera while the game is in front.",
                     { { nullptr, "On" }, { "0", "Off" } },
                     0,
                     false,
                     nullptr,
                     1 });
    rows.push_back({ "DOD3_CAMERA_SPEED",
                     "Camera Speed",
                     "Console keeps the stick's speed at any frame rate.",
                     { { nullptr, "Console" }, { "original", "Original" } },
                     0,
                     false,
                     nullptr,
                     1 });
    rows.push_back({ "DOD3_LAUNCHER",
                     "Launcher at Start",
                     "Open this at start. Hold Shift (Option on a Mac) to open it anyway.",
                     { { nullptr, "On" }, { "0", "Off" } },
                     0,
                     false,
                     nullptr,
                     1 });
}

Tab tab_of(const char* key)
{
    static const char* const display[] = { "RSX_DISPLAY", "RSX_WINDOW", "RSX_SCALE", "DOD3_ASPECT", "DOD3_FPS",
                                           "RSX_VSYNC" };
    static const char* const controls[] = { "DOD3_PROMPTS", "DOD3_MOUSE", "DOD3_MOUSE_SENS", "DOD3_CAMERA_SPEED" };
    static const char* const system[] = { "DOD3_SKIP_INTRO", "DOD3_UNFOCUSED", "RSX_BACKEND", "DOD3_LAUNCHER" };
    for (const char* k : display)
        if (!strcmp(k, key)) return kDisplay;
    for (const char* k : controls)
        if (!strcmp(k, key)) return kControls;
    for (const char* k : system)
        if (!strcmp(k, key)) return kSystem;
    return kGraphics;
}

/* The order within a tab: as listed above, the rest after. */
int order_of(const char* key)
{
    static const char* const all[] = { "RSX_DISPLAY",  "RSX_WINDOW",      "RSX_SCALE",       "DOD3_ASPECT",
                                       "DOD3_FPS",     "RSX_VSYNC",       "RSX_AA",          "RSX_ANISO",
                                       "DOD3_FOV",     "DOD3_PROMPTS",    "DOD3_MOUSE",      "DOD3_MOUSE_SENS",
                                       "DOD3_CAMERA_SPEED", "DOD3_SKIP_INTRO", "DOD3_UNFOCUSED", "RSX_BACKEND",
                                       "DOD3_LAUNCHER" };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        if (!strcmp(all[i], key)) return (int)i;
    return 100;
}

/* The keyboard's keys (src/input/kbm.cpp has the same names and defaults). */
struct Bind {
    const char* key;
    const char* label;
    const char* defaults;
};
const Bind kBinds[] = {
    { "KEY_MOVE_UP", "Move Forward", "W" },      { "KEY_MOVE_DOWN", "Move Back", "S" },
    { "KEY_MOVE_LEFT", "Move Left", "A" },       { "KEY_MOVE_RIGHT", "Move Right", "D" },
    { "KEY_CROSS", "Cross", "Space" },           { "KEY_CIRCLE", "Circle", "E Backspace" },
    { "KEY_SQUARE", "Square", "Mouse1" },        { "KEY_TRIANGLE", "Triangle", "Mouse2" },
    { "KEY_L1", "L1", "Q Mouse3" },              { "KEY_R1", "R1", "Shift" },
    { "KEY_L2", "L2", "R" },                     { "KEY_R2", "R2", "F" },
    { "KEY_L3", "L3", "V C" },                   { "KEY_R3", "R3", "V X" },
    { "KEY_START", "Start", "Esc Enter" },       { "KEY_SELECT", "Select", "Tab" },
    { "KEY_UP", "D-Pad Up", "Up 1" },            { "KEY_DOWN", "D-Pad Down", "Down 3" },
    { "KEY_LEFT", "D-Pad Left", "Left 4" },      { "KEY_RIGHT", "D-Pad Right", "Right 2" },
    { "KEY_LOOK_UP", "Camera Up", "none" },      { "KEY_LOOK_DOWN", "Camera Down", "none" },
    { "KEY_LOOK_LEFT", "Camera Left", "WheelUp" }, { "KEY_LOOK_RIGHT", "Camera Right", "WheelDown" },
};

/* An ImGui key as kbm.cpp names it; nullptr if it has no name there. */
const char* key_name(ImGuiKey k)
{
    static char one[8];
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) {
        one[0] = (char)('A' + (k - ImGuiKey_A));
        one[1] = 0;
        return one;
    }
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) {
        one[0] = (char)('0' + (k - ImGuiKey_0));
        one[1] = 0;
        return one;
    }
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) {
        snprintf(one, sizeof one, "F%d", 1 + (k - ImGuiKey_F1));
        return one;
    }
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) {
        snprintf(one, sizeof one, "Num%d", (int)(k - ImGuiKey_Keypad0));
        return one;
    }
    switch (k) {
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter: return "Enter";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Backspace: return "Backspace";
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift: return "Shift";
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl: return "Ctrl";
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt: return "Alt";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Delete: return "Delete";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
#ifndef __APPLE__
    case ImGuiKey_CapsLock: return "CapsLock";
#endif
    case ImGuiKey_Minus: return "Minus";
    case ImGuiKey_Equal: return "Equals";
    case ImGuiKey_LeftBracket: return "LBracket";
    case ImGuiKey_RightBracket: return "RBracket";
    case ImGuiKey_Semicolon: return "Semicolon";
    case ImGuiKey_Apostrophe: return "Quote";
    case ImGuiKey_Comma: return "Comma";
    case ImGuiKey_Period: return "Period";
    case ImGuiKey_Slash: return "Slash";
    case ImGuiKey_Backslash: return "Backslash";
    case ImGuiKey_GraveAccent: return "Grave";
    default: return nullptr;
    }
}

/* ---- the mods the launcher knows ------------------------------------------------ */

struct ModPart {
    const char* id;
    const char* name;
    const char* needs;   /* what it is for, if not the base game */
};
struct ModGroup {
    const char* title;
    const char* by;
    const char* about;
    std::vector<ModPart> parts;
};
const ModGroup kSpanish = { "Spanish Translation",
                            "Traducciones Vacuno \xC2\xB7 TraduSquare v1.1",
                            "The whole game in Spanish: text, menus, the title screen and subtitled movies. "
                            "Add the US release's downloads as they are: the two .pkg files and MOVIES.zip, "
                            "and the DLC and voice packages if you have those.",
                            { { "ES-GAME", "Story and mission text", nullptr },
                              { "ES-DATA", "Title screen and menus", nullptr },
                              { "ES-MOVIES", "Movies", nullptr },
                              { "ES-DLC", "DLC chapters", "with the DLC" },
                              { "ES-JPV", "Japanese voices' movies", "with the Japanese Voice Pack" } } };
const ModGroup kUndub = { "DLC Undub",
                          "Canzah and Slow \xC2\xB7 prototype 0.707",
                          "Japanese voices for the six DLC chapters, retimed to match. Follows the game's own "
                          "Voice setting (Change Outfits/Music > Voice). Add its folder, the one holding GAME "
                          "and ISO, with Add Folder.",
                          { { "UNDUB", "Japanese voices for the DLC chapters", nullptr } } };

/* ---- the launcher -------------------------------------------------------------- */

enum class Panel { None, Mods, Dlc, Settings };

struct Launcher {
    fs::path base, ini;
    Fonts F;
    Status have;
    std::map<std::string, std::string> kv;   /* dod3.ini */
    std::vector<Dod3RowSpec> rows;
    ImTextureData* art = nullptr;

    float s = 1.0f, W = 0, H = 0, dt = 0;
    double now = 0;
    std::map<ImGuiID, float> anim;
    Panel panel = Panel::None;
    double panel_at = -10;
    bool focus_panel = false;          /* the keyboard or a pad opened it: focus its first item */
    Panel refocus = Panel::None;       /* closed by them: focus its menu entry */
    int tab = kDisplay;
    int capture = -1, capture_frames = 0;   /* a key binding being set */
    int cooldown = 0;                       /* frames during which Esc and activations are ignored */
    std::string toast;
    double toast_at = -10;
    int frames = 0, grabbed = -1;
    std::string grab;
    bool auto_quit = false, auto_install = false;
    std::vector<ImGuiKey> test_keys;   /* DOD3_LAUNCHER_KEYS */
    size_t test_key = 0;

    /* adding or removing: the files being recognised, then the plan, then the install */
    enum class Flow { Idle, Review, Working, Done, Failed };
    Flow flow = Flow::Idle;
    struct Check {
        fs::path path;
        std::future<std::pair<Source, std::string>> fut;
    };
    std::vector<Check> checks;
    Plan plan;
    std::vector<std::string> notes, removing;
    bool remove_legacy = false;   /* a mod an older setup installed: removing it takes the update */
    std::thread worker;
    std::atomic<bool> cancel{ false }, finished{ false };
    std::atomic<uint64_t> done{ 0 }, total{ 1 };
    std::mutex what_mu;
    std::string what, error;
    bool ok = false;

    float px(float design) const { return design * s; }
    /* an item the mouse, the keyboard and a pad all reach: ImGui leaves an
     * invisible button out of its navigation unless asked */
    static bool hit(const char* id, const ImVec2& size)
    {
        return ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    }
    /* the last item under the mouse, or focused while the keyboard or a pad is in use */
    static bool hot_item() { return ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible); }

    float& anim_of(ImGuiID id) { return anim[id]; }
    void take_focus()
    {
        if (!focus_panel) return;
        ImGui::SetKeyboardFocusHere();
        focus_panel = false;
    }
    void approach(float& v, float target, float speed = 14.0f)
    {
        v += (target - v) * (1.0f - std::exp(-speed * dt));
    }

    /* ---- text ---- */

    ImVec2 text_size(ImFont* f, float size, const char* t, float wrap = 0.0f) const
    {
        return f->CalcTextSizeA(px(size), FLT_MAX, wrap, t);
    }
    /* letter-spaced, for the title (ASCII) */
    float tracked_width(ImFont* f, float size, const char* t, float track) const
    {
        float w = 0;
        for (const char* c = t; *c; c++) w += f->CalcTextSizeA(px(size), FLT_MAX, 0, c, c + 1).x + (c[1] ? px(track) : 0);
        return w;
    }
    void tracked(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, float track)
    {
        for (const char* c = t; *c; c++) {
            dl->AddText(f, px(size), p, col, c, c + 1);
            p.x += f->CalcTextSizeA(px(size), FLT_MAX, 0, c, c + 1).x + px(track);
        }
    }
    /* ImGui text in a font and size, wrapped to the region */
    float wrap_right = 0;   /* inside a card: its padding's edge (screen x) */
    void para(ImFont* f, float size, ImU32 col, const char* t)
    {
        ImGui::PushFont(f, size);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushTextWrapPos(wrap_right > 0 ? wrap_right - ImGui::GetWindowPos().x : 0.0f);
        ImGui::TextUnformatted(t);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void say(const std::string& t)
    {
        toast = t;
        toast_at = now;
    }

    /* ---- widgets ---- */

    /* 0 a quiet button, 1 the accent, 2 a red outline */
    bool button(const char* label, float w, int kind = 0, bool enabled = true)
    {
        ImGui::PushID(label);
        const float h = px(40);
        const ImVec2 ts = text_size(F.semibold, 15, label);
        const ImVec2 sz(w > 0 ? px(w) : ts.x + px(36), h);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!enabled);
        const bool pressed = hit("##b", sz) && cooldown == 0;
        const bool hot = hot_item();
        const bool held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        float& a = anim_of(ImGui::GetItemID());
        approach(a, hot && enabled ? 1.0f : 0.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float k = enabled ? 1.0f : 0.4f;
        ImU32 bg, fg = alpha(kText, k);
        if (kind == 1)
            bg = alpha(mix(kAccent, kAccentHi, a + (held ? 0.4f : 0.0f)), k);
        else if (kind == 2)
            bg = alpha(kErr, 0.10f + 0.18f * a);
        else
            bg = IM_COL32(255, 255, 255, (int)((14 + 18 * a + (held ? 10 : 0)) * k));
        dl->AddRectFilled(p, p + sz, bg, px(10));
        if (kind == 2) {
            dl->AddRect(p, p + sz, alpha(kErr, 0.55f + 0.45f * a), px(10), 0, px(1.2f));
            fg = alpha(mix(kErr, kText, a), k);
        }
        dl->AddText(F.semibold, px(15), ImVec2(p.x + (sz.x - ts.x) / 2, p.y + (sz.y - ts.y) / 2), fg, label);
        ImGui::PopID();
        return pressed && enabled;
    }

    /* a light: filled when on */
    void dot(ImDrawList* dl, ImVec2 c, ImU32 col, bool filled)
    {
        if (filled)
            dl->AddCircleFilled(c, px(4.5f), col);
        else
            dl->AddCircle(c, px(4.5f), col, 0, px(1.4f));
    }

    void tick(ImDrawList* dl, ImVec2 c, ImU32 col)
    {
        const float r = px(5.5f);
        const ImVec2 pts[3] = { ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.3f, c.y + r * 0.7f), ImVec2(c.x + r, c.y - r * 0.8f) };
        dl->AddPolyline(pts, 3, col, 0, px(2.0f));
    }

    void pill(ImDrawList* dl, ImVec2 right_top, const char* t, ImU32 col)
    {
        const ImVec2 ts = text_size(F.semibold, 12, t);
        const ImVec2 p0(right_top.x - ts.x - px(22), right_top.y), p1(right_top.x, right_top.y + ts.y + px(8));
        dl->AddRectFilled(p0, p1, alpha(col, 0.16f), px(20));
        dl->AddText(F.semibold, px(12), ImVec2(p0.x + px(11), p0.y + px(4)), col, t);
    }

    /* ---- the install, on its own thread ---- */

    void add(const std::vector<fs::path>& paths)
    {
        if (paths.empty()) return;
        notes.clear();
        for (const fs::path& p : paths)
            checks.push_back({ p, std::async(std::launch::async, [p] {
                                   Source s;
                                   std::string err;
                                   if (!identify(p, &s, &err)) s = Source();
                                   return std::make_pair(s, err);
                               }) });
        flow = Flow::Review;
    }

    void poll_checks()
    {
        for (size_t i = 0; i < checks.size();) {
            if (checks[i].fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                i++;
                continue;
            }
            auto [src, err] = checks[i].fut.get();
            const std::string name =
                utf8(checks[i].path.filename().empty() ? checks[i].path : checks[i].path.filename());
            switch (src.kind) {
            case SourceKind::Update: plan.update = src; break;
            case SourceKind::Dlc: {
                auto& d = plan.dlc;
                d.erase(std::remove_if(d.begin(), d.end(), [&](const Source& o) { return o.id == src.id; }), d.end());
                d.push_back(src);
                break;
            }
            case SourceKind::Mod: {
                auto& m = plan.mods;
                m.erase(std::remove_if(m.begin(), m.end(), [&](const Source& o) { return o.id == src.id; }), m.end());
                m.push_back(src);
                break;
            }
            case SourceKind::Disc:
            case SourceKind::Eboot:
                notes.push_back(name + ": this is the game itself. To install it again, use Game Files.");
                break;
            default: notes.push_back(name + ": " + err); break;
            }
            checks.erase(checks.begin() + (long)i);
        }
    }

    bool plan_empty() const
    {
        return plan.dlc.empty() && plan.mods.empty() && !plan.update.set() && plan.remove_ids.empty() &&
               !plan.remove_mods;
    }
    bool remove_blocked() const { return remove_legacy && !plan.update.set(); }

    void start_remove(const ModGroup& g)
    {
        plan = Plan();
        notes.clear();
        removing.clear();
        remove_legacy = false;
        for (const ModPart& p : g.parts)
            for (const auto& m : have.mods)
                if (m.first == p.id) {
                    if (mod_removable(base, p.id))
                        plan.remove_ids.push_back(p.id);
                    else
                        remove_legacy = true;
                }
        if (remove_legacy) plan.remove_mods = true;   /* with the update, as the setup did */
        removing.push_back(g.title);
        flow = Flow::Review;
    }
    void start_remove_one(const std::string& id, const std::string& name)
    {
        plan = Plan();
        notes.clear();
        removing = { name };
        remove_legacy = !mod_removable(base, id);
        if (remove_legacy)
            plan.remove_mods = true;
        else
            plan.remove_ids.push_back(id);
        flow = Flow::Review;
    }

    void start_install()
    {
        cancel = false;
        finished = false;
        done = 0;
        total = 1;
        {
            std::lock_guard<std::mutex> g(what_mu);
            what.clear();
        }
        const Plan p = plan;
        worker = std::thread([this, p] {
            std::string err;
            const bool r = install(
                base, p,
                [this](uint64_t d, uint64_t t, const std::string& w) {
                    done = d;
                    total = t ? t : 1;
                    std::lock_guard<std::mutex> g(what_mu);
                    what = w;
                    return !cancel.load();
                },
                &err);
            ok = r;
            error = err;
            finished = true;
        });
        flow = Flow::Working;
    }

    void end_flow()
    {
        flow = Flow::Idle;
        plan = Plan();
        notes.clear();
        removing.clear();
        remove_legacy = false;
    }

    /* ---- the background, the title, the menu ---- */

    void background()
    {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(9, 9, 11, 255));
        if (art) {
            /* cover the window, drifting slowly */
            const float aw = (float)art->Width, ah = (float)art->Height;
            const float k = std::max(W / aw, H / ah) * (1.05f + 0.02f * (float)std::sin(now * 0.05));
            const float dw = aw * k, dh = ah * k;
            const float ox = (W - dw) / 2 + (float)std::sin(now * 0.031) * px(10), oy = (H - dh) / 2;
            dl->AddImage(art->GetTexRef(), ImVec2(ox, oy), ImVec2(ox + dw, oy + dh));
        } else {
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(28, 10, 14, 255), IM_COL32(12, 10, 12, 255),
                                        IM_COL32(9, 9, 11, 255), IM_COL32(16, 9, 11, 255));
        }
        /* the left darkened for the menu, the bottom for the footer */
        const ImU32 a = IM_COL32(8, 8, 10, 236), z = IM_COL32(8, 8, 10, 0);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W * 0.62f, H), a, z, z, a);
        dl->AddRectFilledMultiColor(ImVec2(0, H * 0.62f), ImVec2(W, H), z, z, IM_COL32(8, 8, 10, 210),
                                    IM_COL32(8, 8, 10, 210));
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H * 0.18f), IM_COL32(8, 8, 10, 120),
                                    IM_COL32(8, 8, 10, 120), z, z);
        const float open = panel == Panel::None ? 0.0f : ease_out((float)(now - panel_at) / 0.25f);
        if (open > 0) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 6, 8, (int)(110 * open)));
    }

    void title()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p(px(72), px(70));
        tracked(dl, F.display, 50, p, kText, "DRAKENGARD", 2.5f);
        const float w = tracked_width(F.display, 50, "DRAKENGARD", 2.5f);
        tracked(dl, F.display, 50, ImVec2(p.x + w + px(16), p.y), kAccentHi, "3", 0);
        const float y = p.y + px(66);
        dl->AddRectFilled(ImVec2(p.x, y + px(7)), ImVec2(p.x + px(34), y + px(9)), kAccent);
        tracked(dl, F.semibold, 13, ImVec2(p.x + px(48), y), kDim, "RECOMPILED", 4.0f);
    }

    std::string mods_summary() const
    {
        bool es = false, undub = false;
        size_t other = 0;
        for (const auto& m : have.mods) {
            if (m.first.rfind("ES-", 0) == 0)
                es = true;
            else if (m.first == "UNDUB")
                undub = true;
            else
                other++;
        }
        std::string t;
        auto add = [&](const std::string& x) { t += (t.empty() ? "" : " \xC2\xB7 ") + x; };
        if (es) add("Spanish translation");
        if (undub) add("DLC Undub");
        if (other) add(std::to_string(other) + (other == 1 ? " other" : " others"));
        return t.empty() ? "None installed" : t;
    }

    std::string settings_summary() const
    {
        std::string t;
        auto add = [&](const std::string& x) { t += (t.empty() ? "" : " \xC2\xB7 ") + x; };
        for (const char* key : { "RSX_SCALE", "DOD3_FPS", "RSX_DISPLAY" })
            for (const Dod3RowSpec& r : rows)
                if (!strcmp(r.key, key)) {
                    std::string v = value_text(r);
                    const size_t paren = v.find(" (");
                    if (paren != std::string::npos) v.resize(paren);
                    if (!strcmp(key, "DOD3_FPS") && !v.empty() && v[0] >= '0' && v[0] <= '9') v += " fps";
                    add(v);
                }
        return t;
    }

    /* one entry: returns true when chosen */
    bool menu_item(const char* id, const char* label, const char* sub, float y, bool big, bool active, bool enabled)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x = px(72), h = big ? px(64) : px(52);
        ImGui::SetCursorScreenPos(ImVec2(x - px(20), y));
        ImGui::BeginDisabled(!enabled);
        const bool pressed = hit(id, ImVec2(px(420), h)) && cooldown == 0;
        const bool hot = enabled && hot_item();
        ImGui::EndDisabled();
        float& a = anim_of(ImGui::GetItemID());
        approach(a, hot || active ? 1.0f : 0.0f, 16.0f);
        const float lsz = big ? 34.0f : 22.0f;
        ImFont* lf = big ? F.display : F.medium;
        const ImVec2 ls = text_size(lf, lsz, label);
        const float tx = x + px(12) * a, ty = y + (sub ? px(4) : (h - ls.y) / 2);
        /* the accent bar grows in from the middle */
        const float bh = ls.y * 0.8f * a;
        const float cy = ty + ls.y / 2;
        if (a > 0.01f) dl->AddRectFilled(ImVec2(x - px(14), cy - bh / 2), ImVec2(x - px(11), cy + bh / 2), kAccent);
        ImU32 col = enabled ? mix(alpha(kText, 0.72f), kText, a) : alpha(kText, 0.3f);
        if (big) col = enabled ? mix(kText, IM_COL32(255, 255, 255, 255), a) : alpha(kText, 0.3f);
        if (big) {   /* a play mark */
            const float r = px(9);
            const ImVec2 c(tx + r * 0.6f, cy);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, c.y - r), ImVec2(c.x - r * 0.55f, c.y + r),
                                  ImVec2(c.x + r, c.y), mix(kAccent, kAccentHi, a));
            dl->AddText(lf, px(lsz), ImVec2(tx + px(28), ty), col, label);
        } else {
            dl->AddText(lf, px(lsz), ImVec2(tx, ty), col, label);
        }
        if (sub && *sub)
            dl->AddText(F.regular, px(13), ImVec2(tx, ty + ls.y + px(1)), alpha(kDim, 0.65f + 0.35f * a), sub);
        return pressed && enabled;
    }

    int menu()
    {
        const bool busy = flow == Flow::Working;
        float y = H * 0.40f;
        int result = -1;
        if (menu_item("##play", "Play", nullptr, y, true, false, !busy)) result = 0;
        /* Enter, or a pad's confirm button, plays straight away when nothing
         * shows the focus yet (ImGui's first press only shows it) */
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.NavVisible && panel == Panel::None && !busy && capture < 0 && cooldown == 0 &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
             ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)))
            result = 0;
        y += px(84);
        auto open = [&](Panel p) {
            if (panel == p) {
                panel = Panel::None;
                return;
            }
            panel = p;
            panel_at = now;
            focus_panel = ImGui::GetIO().NavVisible;
        };
        auto back_here = [&](Panel p) {
            if (refocus != p) return;
            ImGui::SetKeyboardFocusHere();
            refocus = Panel::None;
        };
        back_here(Panel::Mods);
        if (menu_item("##mods", "Mods", mods_summary().c_str(), y, false, panel == Panel::Mods, true)) open(Panel::Mods);
        y += px(62);
        char dlc[64];
        snprintf(dlc, sizeof dlc, "%zu of %zu installed", have.dlc.size(), known_dlc().size());
        back_here(Panel::Dlc);
        if (menu_item("##dlc", "DLC", dlc, y, false, panel == Panel::Dlc, true)) open(Panel::Dlc);
        y += px(62);
        back_here(Panel::Settings);
        if (menu_item("##settings", "Settings", settings_summary().c_str(), y, false, panel == Panel::Settings, true))
            open(Panel::Settings);
        y += px(62);
        if (menu_item("##quit", "Quit", nullptr, y, false, false, !busy)) result = 1;
        return result;
    }

    int footer()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float y = H - px(52);
        char v[64];
        snprintf(v, sizeof v, "Version %s", DOD3_VERSION_STRING);
        dl->AddText(F.regular, px(13), ImVec2(px(72), y), alpha(kDim, 0.8f), v);
        const float vw = text_size(F.regular, 13, v).x;
        /* Game Files: the installer, for the disc or the update */
        const char* gf = "Game Files";
        const ImVec2 gs = text_size(F.semibold, 13, gf);
        const ImVec2 gp(px(72) + vw + px(26), y);
        ImGui::SetCursorScreenPos(ImVec2(gp.x - px(6), gp.y - px(4)));
        const bool busy = flow == Flow::Working;
        ImGui::BeginDisabled(busy);
        const bool pressed = hit("##gamefiles", ImVec2(gs.x + px(12), gs.y + px(8)));
        const bool hot = hot_item();
        ImGui::EndDisabled();
        dl->AddText(F.semibold, px(13), gp, busy ? alpha(kDim, 0.4f) : hot ? kText : alpha(kDim, 0.9f), gf);
        if (hot) dl->AddLine(ImVec2(gp.x, gp.y + gs.y + px(1)), ImVec2(gp.x + gs.x, gp.y + gs.y + px(1)), kAccent, px(1.5f));
        return pressed && cooldown == 0 && !busy ? 2 : -1;
    }

    void toast_draw()
    {
        const float t = (float)(now - toast_at);
        if (toast.empty() || t > 2.2f) return;
        const float a = std::min(1.0f, t / 0.15f) * std::min(1.0f, (2.2f - t) / 0.4f);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 ts = text_size(F.semibold, 14, toast.c_str());
        const ImVec2 c(W / 2, H - px(40) - px(6) * (1 - a));
        const ImVec2 p0(c.x - ts.x / 2 - px(18), c.y - ts.y / 2 - px(9)), p1(c.x + ts.x / 2 + px(18), c.y + ts.y / 2 + px(9));
        dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 30, (int)(240 * a)), px(30));
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, (int)(26 * a)), px(30));
        dl->AddText(F.semibold, px(14), ImVec2(p0.x + px(18), p0.y + px(9)), alpha(kText, a), toast.c_str());
    }

    /* ---- the panels ---- */

    /* the panel's frame and header; returns the content's top-left and size */
    bool panel_frame(const char* heading, const char* sub, ImVec2* body, ImVec2* body_size)
    {
        const float e = ease_out((float)(now - panel_at) / 0.25f);
        const float x0 = W * 0.385f + px(36) * (1 - e), x1 = W - px(56) + px(36) * (1 - e);
        const float y0 = px(56), y1 = H - px(56);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(x0 + px(2), y0 + px(10)), ImVec2(x1 + px(2), y1 + px(14)), IM_COL32(0, 0, 0, (int)(90 * e)),
                          px(20));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), alpha(kPanel, e), px(18));
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), alpha(kLine, e), px(18));
        dl->AddText(F.display, px(30), ImVec2(x0 + px(36), y0 + px(28)), alpha(kText, e), heading);
        if (sub) dl->AddText(F.regular, px(14), ImVec2(x0 + px(36), y0 + px(68)), alpha(kDim, e), sub);
        /* close */
        const float cs = px(34);
        const ImVec2 cp(x1 - px(24) - cs, y0 + px(26));
        ImGui::SetCursorScreenPos(cp);
        bool close = hit("##close", ImVec2(cs, cs)) && cooldown == 0;
        float& a = anim_of(ImGui::GetItemID());
        approach(a, hot_item() ? 1.0f : 0.0f);
        dl->AddCircleFilled(cp + ImVec2(cs / 2, cs / 2), cs / 2, IM_COL32(255, 255, 255, (int)(10 + 22 * a)));
        const float r = px(6);
        const ImVec2 c = cp + ImVec2(cs / 2, cs / 2);
        dl->AddLine(c - ImVec2(r, r), c + ImVec2(r, r), alpha(kText, 0.6f + 0.4f * a), px(1.6f));
        dl->AddLine(c + ImVec2(-r, r), c + ImVec2(r, -r), alpha(kText, 0.6f + 0.4f * a), px(1.6f));
        *body = ImVec2(x0 + px(36), y0 + (sub ? px(104) : px(84)));
        *body_size = ImVec2(x1 - x0 - px(72), y1 - body->y - px(28));
        return close;
    }

    /* a card around what is drawn between card_begin and card_end */
    float card_x0 = 0, card_x1 = 0;
    void card_begin()
    {
        card_x0 = ImGui::GetCursorScreenPos().x;
        card_x1 = card_x0 + ImGui::GetContentRegionAvail().x - px(4);
        wrap_right = card_x1 - px(18);
        ImGui::GetWindowDrawList()->ChannelsSplit(2);
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, px(14)));
        ImGui::Indent(px(18));
    }
    void card_end()
    {
        ImGui::Unindent(px(18));
        ImGui::Dummy(ImVec2(0, px(10)));
        ImGui::EndGroup();
        wrap_right = 0;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a(card_x0, ImGui::GetItemRectMin().y), b(card_x1, ImGui::GetItemRectMax().y);
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(a, b, kCard, px(14));
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 14), px(14));
        dl->ChannelsMerge();
        ImGui::Dummy(ImVec2(0, px(12)));
    }

    void mod_card(const ModGroup& g)
    {
        int in = 0;
        for (const ModPart& p : g.parts)
            for (const auto& m : have.mods) in += m.first == p.id;
        card_begin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float right = card_x1 - px(18);
        ImGui::PushFont(F.semibold, 19);
        ImGui::TextUnformatted(g.title);
        ImGui::PopFont();
        const ImVec2 tp = ImGui::GetItemRectMin();
        if (in == (int)g.parts.size())
            pill(dl, ImVec2(right, tp.y + px(2)), "Installed", kOk);
        else if (in > 0)
            pill(dl, ImVec2(right, tp.y + px(2)), "Partly installed", kWarn);
        else
            pill(dl, ImVec2(right, tp.y + px(2)), "Not installed", kFaint);
        para(F.medium, 13, kDim, g.by);
        ImGui::Dummy(ImVec2(0, px(4)));
        para(F.regular, 14, alpha(kText, 0.82f), g.about);
        if (g.parts.size() > 1 || in < (int)g.parts.size()) {
            ImGui::Dummy(ImVec2(0, px(4)));
            for (const ModPart& p : g.parts) {
                bool on = false;
                for (const auto& m : have.mods) on |= m.first == p.id;
                const ImVec2 c = ImGui::GetCursorScreenPos();
                ImGui::PushFont(F.regular, 14);
                const float lh = ImGui::GetTextLineHeight();
                if (on)
                    tick(dl, ImVec2(c.x + px(6), c.y + lh / 2), kOk);
                else
                    dot(dl, ImVec2(c.x + px(6), c.y + lh / 2), kFaint, false);
                ImGui::SetCursorScreenPos(ImVec2(c.x + px(22), c.y));
                ImGui::PushStyleColor(ImGuiCol_Text, on ? kText : kDim);
                ImGui::TextUnformatted(p.name);
                ImGui::PopStyleColor();
                if (p.needs) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, kFaint);
                    ImGui::TextUnformatted(p.needs);
                    ImGui::PopStyleColor();
                }
                ImGui::PopFont();
            }
        }
        if (!strcmp(g.title, kUndub.title) && !have.dlc.count("ADDJAPANESEVOICE")) {
            ImGui::Dummy(ImVec2(0, px(4)));
            para(F.medium, 13, kWarn, "Needs the Japanese Voice Pack: add its package on the DLC page first.");
        }
        if (in > 0) {
            ImGui::Dummy(ImVec2(0, px(8)));
            ImGui::PushID(g.title);
            if (button("Remove", 0, 2)) start_remove(g);
            ImGui::PopID();
        }
        card_end();
    }

    /* the foot of a panel: a line of text and the Add buttons */
    void panel_foot(ImVec2 body, ImVec2 size, const char* note, bool folder)
    {
        const float y = body.y + size.y - px(44);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(body.x, y - px(16)), ImVec2(body.x + size.x, y - px(16)), kLine);
        ImGui::SetCursorScreenPos(ImVec2(body.x, y));
        take_focus();
        add_buttons(folder);
        const float bx = ImGui::GetItemRectMax().x + px(18);
        dl->AddText(F.regular, px(13), ImVec2(bx, y + px(4)), kDim, note, nullptr, body.x + size.x - bx);
    }

    void mods_panel(ImVec2 body, ImVec2 size)
    {
        panel_foot(body, size, "Packages (.pkg), a movie archive (.zip) or a mod's folder, as downloaded. Your own "
                               "game files are never changed.", true);
        ImGui::SetCursorScreenPos(body);
        ImGui::BeginChild("##mods", ImVec2(size.x, size.y - px(64)), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground);
        mod_card(kSpanish);
        mod_card(kUndub);
        for (const auto& m : have.mods) {
            bool known = false;
            for (const ModGroup* g : { &kSpanish, &kUndub })
                for (const ModPart& p : g->parts) known |= m.first == p.id;
            if (known) continue;
            card_begin();
            ImGui::PushFont(F.semibold, 17);
            ImGui::TextUnformatted(m.second.c_str());
            ImGui::PopFont();
            para(F.regular, 13, kDim, "Installed from a homebrew package laid out as the console installs it.");
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::PushID(m.first.c_str());
            if (button("Remove", 0, 2)) start_remove_one(m.first, m.second);
            ImGui::PopID();
            card_end();
        }
        ImGui::EndChild();
    }

    void add_buttons(bool folder)
    {
        if (button(folder ? "Add Files..." : "Add DLC Packages...", 0, 1))
            add(ui_pick_files(folder ? "Add mods" : "Add DLC packages",
                              { { "Drakengard 3 packages and archives (.pkg, .zip)", "*.pkg;*.zip" },
                                { "All files", "*.*" } }));
        if (folder) {
            ImGui::SameLine(0, px(10));
            if (button("Add Folder...", 0, 0)) {
                const fs::path p = ui_pick_folder("A mod's folder (the DLC Undub's holds GAME and ISO)");
                if (!p.empty()) add({ p });
            }
        }
    }

    void dlc_panel(ImVec2 body, ImVec2 size)
    {
        panel_foot(body, size, "The PSN packages you own (UP0082-NPUB31251_00-...). No licence files are needed.",
                   false);
        ImGui::SetCursorScreenPos(body);
        ImGui::BeginChild("##dlc", ImVec2(size.x, size.y - px(64)), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground);
        struct Group {
            const char* title;
            const char* prefix;
        };
        const Group groups[] = { { "Story", "ADDSCENARIO" },
                                 { "Voices", "ADDJAPANESEVOICE" },
                                 { "Music", "ADDSONG" },
                                 { "Outfits and Weapons", "ADDWEAPONCLOTHE" } };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cw = (ImGui::GetContentRegionAvail().x - px(12)) / 2;
        for (const Group& g : groups) {
            ImGui::PushFont(F.semibold, 13);
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::TextUnformatted(g.title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
            int col = 0;
            for (const DlcInfo& d : known_dlc()) {
                if (strncmp(d.id, g.prefix, strlen(g.prefix))) continue;
                const bool in = have.dlc.count(d.id) != 0;
                if (col) ImGui::SameLine(0, px(12));
                const ImVec2 p = ImGui::GetCursorScreenPos(), sz(cw, px(44));
                ImGui::Dummy(sz);
                dl->AddRectFilled(p, p + sz, in ? IM_COL32(255, 255, 255, 12) : IM_COL32(255, 255, 255, 5), px(10));
                if (in)
                    tick(dl, ImVec2(p.x + px(18), p.y + sz.y / 2), kOk);
                else
                    dot(dl, ImVec2(p.x + px(18), p.y + sz.y / 2), kFaint, false);
                const ImVec2 ts = text_size(F.medium, 14, d.name);
                dl->AddText(F.medium, px(14), ImVec2(p.x + px(36), p.y + (sz.y - ts.y) / 2), in ? kText : kDim, d.name);
                col ^= 1;
            }
            ImGui::Dummy(ImVec2(0, px(10)));
        }
        para(F.regular, 13, kFaint,
             "The Japanese Voice Pack brings Japanese voices to the main story (Change Outfits/Music > Voice), and "
             "with the DLC Undub to the DLC chapters too.");
        ImGui::EndChild();
    }

    /* adding or removing, in place of the panel's own page */
    void flow_page(ImVec2 body, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(body);
        ImGui::BeginChild("##flow", size, ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (flow == Flow::Review) {
            ImGui::PushFont(F.semibold, 19);
            ImGui::TextUnformatted(!removing.empty() ? "Remove" : "Ready to Install");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(6)));
            auto row = [&](const std::string& name, uint64_t bytes, ImU32 col) {
                const ImVec2 c = ImGui::GetCursorScreenPos();
                ImGui::PushFont(F.medium, 15);
                const float lh = ImGui::GetTextLineHeight();
                dot(dl, ImVec2(c.x + px(6), c.y + lh / 2), col, true);
                ImGui::SetCursorScreenPos(ImVec2(c.x + px(22), c.y));
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopFont();
                if (bytes) {
                    ImGui::SameLine();
                    ImGui::PushFont(F.regular, 14);
                    ImGui::PushStyleColor(ImGuiCol_Text, kFaint);
                    ImGui::TextUnformatted(gb(bytes).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                ImGui::Dummy(ImVec2(0, px(2)));
            };
            for (const std::string& r : removing) row(r, 0, kErr);
            if (plan.update.set()) row("Update 1.01", plan.update.bytes, kOk);
            for (const Source& d : plan.dlc) row(d.name, d.bytes, kOk);
            for (const Source& m : plan.mods) row(m.name, m.bytes, kOk);
            if (!checks.empty()) {
                static const char* dots[] = { "", ".", "..", "..." };
                char t[64];
                snprintf(t, sizeof t, "Checking %zu file%s%s", checks.size(), checks.size() == 1 ? "" : "s",
                         dots[(int)(now * 3) % 4]);
                para(F.regular, 14, kDim, t);
            }
            ImGui::Dummy(ImVec2(0, px(6)));
            for (const std::string& n : notes) para(F.regular, 14, kErr, n.c_str());
            if (needs_voice_pack(plan, have))
                para(F.medium, 14, kWarn,
                     "The DLC Undub needs the Japanese Voice Pack: add its package too (or install it first).");
            if (remove_blocked())
                para(F.medium, 14, kWarn,
                     "An older version of the setup installed this mod, without a list of its files. Add the 1.01 "
                     "update package (Add Files) to remove it: the update puts back the files the mod replaced, "
                     "and all the mods are removed.");
            if (remove_legacy && plan.update.set())
                para(F.regular, 14, kDim, "All the installed mods are removed, and the update installed again.");
            const uint64_t need = plan.bytes(), space = free_bytes(base);
            if (need) {
                char t[96];
                snprintf(t, sizeof t, "Needs %s; %s free.", gb(need).c_str(), gb(space).c_str());
                para(F.regular, 13, kFaint, t);
            }
            const bool full = space && need > space;
            if (full) para(F.medium, 14, kErr, "Not enough space on the drive.");
            ImGui::Dummy(ImVec2(0, px(14)));
            const bool can = checks.empty() && !plan_empty() && !needs_voice_pack(plan, have) && !remove_blocked() &&
                             !full;
            take_focus();
            if (button(!removing.empty() ? "Remove" : "Install", 140, !removing.empty() ? 2 : 1, can) ||
                (auto_install && can && frames > 60))
                start_install();
            ImGui::SameLine(0, px(10));
            if (button("Add Files...", 0, 0, checks.empty()))
                add(ui_pick_files("Add files", { { "Drakengard 3 packages and archives (.pkg, .zip)", "*.pkg;*.zip" },
                                                 { "All files", "*.*" } }));
            ImGui::SameLine(0, px(10));
            if (button("Cancel", 0, 0, checks.empty())) end_flow();
        } else if (flow == Flow::Working) {
            std::string w;
            {
                std::lock_guard<std::mutex> g(what_mu);
                w = what;
            }
            ImGui::PushFont(F.semibold, 19);
            ImGui::TextUnformatted(w.empty() ? "Starting..." : w.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(14)));
            const double f = (double)done / (double)(total ? total.load() : 1);
            const ImVec2 p = ImGui::GetCursorScreenPos(), sz(ImGui::GetContentRegionAvail().x, px(8));
            ImGui::Dummy(sz);
            dl->AddRectFilled(p, p + sz, IM_COL32(255, 255, 255, 18), px(4));
            dl->AddRectFilled(p, ImVec2(p.x + sz.x * (float)std::clamp(f, 0.0, 1.0), p.y + sz.y), kAccentHi, px(4));
            ImGui::Dummy(ImVec2(0, px(8)));
            char t[96];
            snprintf(t, sizeof t, "%s of %s", gb(done).c_str(), gb(total).c_str());
            para(F.regular, 13, kDim, t);
            ImGui::Dummy(ImVec2(0, px(14)));
            if (button(cancel ? "Cancelling..." : "Cancel", 0, 0, !cancel)) cancel = true;
        } else {
            const bool good = flow == Flow::Done;
            ImGui::PushFont(F.semibold, 19);
            ImGui::TextUnformatted(good ? (!removing.empty() ? "Removed" : "Installed") : cancel ? "Cancelled" : "Failed");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(6)));
            if (!good && !cancel) para(F.regular, 14, kErr, error.c_str());
            if (!good) para(F.regular, 14, kDim, "The part that was being installed was taken out again; the parts before it stay.");
            ImGui::Dummy(ImVec2(0, px(14)));
            if (button("Done", 120, 1)) end_flow();
        }
        ImGui::EndChild();
    }

    /* ---- settings ---- */

    int current(const Dod3RowSpec& r, std::string* other = nullptr) const
    {
        const auto it = kv.find(r.key);
        const char* v = it == kv.end() ? nullptr : it->second.c_str();
        for (size_t i = 0; i < r.choices.size(); i++) {
            const char* c = r.choices[i].first;
            if (!v || !*v) {
                if (!c) return (int)i;
                continue;
            }
            if (c && !strcmp(c, v)) return (int)i;
            if (c && !strcmp(r.key, "RSX_SCALE") && atof(c) == atof(v)) return (int)i;
        }
        if (!v || !*v) return r.def;
        if (other) *other = v;
        return -1;
    }
    std::string value_text(const Dod3RowSpec& r) const
    {
        std::string other;
        const int i = current(r, &other);
        return i >= 0 ? r.choices[i].second : other;
    }

    void set_value(const Dod3RowSpec& r, int i)
    {
        const char* v = r.choices[i].first;
        if (v)
            kv[r.key] = v;
        else
            kv.erase(r.key);
        say(dod3::ini_write(ini, { { r.key, v } }) ? "Saved" : "Could not save dod3.ini");
    }

    /* Left / Right for the focused row `id`, which owns them meanwhile (ImGui's
     * navigation reads only keys nobody owns, so the focus stays) */
    bool step_keys(ImGuiID id, int* dir)
    {
        static const ImGuiKey less[] = { ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft };
        static const ImGuiKey more[] = { ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight };
        for (ImGuiKey k : less) ImGui::SetKeyOwner(k, id);
        for (ImGuiKey k : more) ImGui::SetKeyOwner(k, id);
        for (ImGuiKey k : less)
            if (ImGui::IsKeyPressed(k, ImGuiInputFlags_Repeat, id)) {
                *dir = -1;
                return true;
            }
        for (ImGuiKey k : more)
            if (ImGui::IsKeyPressed(k, ImGuiInputFlags_Repeat, id)) {
                *dir = 1;
                return true;
            }
        return false;
    }

    void setting_row(const Dod3RowSpec& r)
    {
        ImGui::PushID(r.key);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const float sw = px(250);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float sx = p.x + w - sw - px(10), dw = sx - p.x - px(34);
        const ImVec2 ds = text_size(F.regular, 13, r.desc, dw);
        const float h = std::max(px(62), px(33) + ds.y + px(13));
        hit("##row", ImVec2(w, h));
        const bool hot = hot_item();
        float& a = anim_of(ImGui::GetItemID());
        approach(a, hot ? 1.0f : 0.0f);
        int dir = 0;
        if (ImGui::IsItemClicked() && cooldown == 0) dir = ImGui::GetIO().MousePos.x < sx + px(40) ? -1 : 1;
        if (!dir && ImGui::IsItemFocused() && cooldown == 0 && !step_keys(ImGui::GetItemID(), &dir) &&
            ImGui::IsItemActivated() && !ImGui::IsMouseClicked(0))
            dir = 1;
        dl->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(255, 255, 255, (int)(10 * a)), px(10));
        dl->AddText(F.semibold, px(15), ImVec2(p.x + px(14), p.y + px(11)), kText, r.label);
        dl->AddText(F.regular, px(13), ImVec2(p.x + px(14), p.y + px(33)), kDim, r.desc, nullptr, dw);
        /* the value between two arrows */
        std::string other;
        const int cur = current(r, &other);
        const std::string vt = cur >= 0 ? r.choices[cur].second : "Custom (" + other + ")";
        const float cy = p.y + h / 2;
        const ImVec2 b0(sx, cy - px(17)), b1(sx + sw, cy + px(17));
        dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, (int)(12 + 10 * a)), px(9));
        const ImVec2 vs = text_size(F.medium, 15, vt.c_str());
        dl->AddText(F.medium, px(15), ImVec2(sx + (sw - vs.x) / 2, cy - vs.y / 2), kText, vt.c_str());
        auto chevron = [&](float x, int d) {   /* pointing the way d steps */
            const float r2 = px(4.5f);
            const ImVec2 pts[3] = { ImVec2(x - r2 * 0.5f * d, cy - r2), ImVec2(x + r2 * 0.5f * d, cy),
                                    ImVec2(x - r2 * 0.5f * d, cy + r2) };
            dl->AddPolyline(pts, 3, alpha(kText, 0.45f + 0.5f * a), 0, px(1.8f));
        };
        chevron(sx + px(20), -1);
        chevron(sx + sw - px(20), 1);
        /* the dots: where the value is among the choices */
        if (r.choices.size() > 1 && r.choices.size() <= 10) {
            const float gap = px(7), n = (float)r.choices.size();
            const float x0 = sx + sw / 2 - (n - 1) * gap / 2;
            for (size_t i = 0; i < r.choices.size(); i++)
                dl->AddCircleFilled(ImVec2(x0 + gap * (float)i, b1.y + px(6)), px(1.6f),
                                    (int)i == cur ? kAccentHi : IM_COL32(255, 255, 255, 40));
        }
        if (dir && r.choices.size() > 1) {
            const int n = (int)r.choices.size();
            const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + dir + n) % n;
            set_value(r, next);
        }
        ImGui::PopID();
    }

    std::string bind_value(const Bind& b) const
    {
        const auto it = kv.find(b.key);
        return it == kv.end() ? b.defaults : it->second;
    }

    void bind_row(int i)
    {
        const Bind& b = kBinds[i];
        ImGui::PushID(b.key);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x, h = px(44);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = hit("##bind", ImVec2(w - px(96), h));
        const bool hot = hot_item();
        float& a = anim_of(ImGui::GetItemID());
        approach(a, hot || capture == i ? 1.0f : 0.0f);
        if (pressed && cooldown == 0 && capture < 0) {
            capture = i;
            capture_frames = 0;
            ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        }
        dl->AddRectFilled(p, p + ImVec2(w - px(96), h), IM_COL32(255, 255, 255, (int)(9 * a)), px(9));
        if (capture == i) dl->AddRect(p, p + ImVec2(w - px(96), h), alpha(kAccentHi, 0.9f), px(9), 0, px(1.5f));
        const ImVec2 ls = text_size(F.medium, 14, b.label);
        dl->AddText(F.medium, px(14), ImVec2(p.x + px(14), p.y + (h - ls.y) / 2), kText, b.label);
        /* the keys, as caps */
        float x = p.x + px(190);
        if (capture == i) {
            const char* t = "Press a key or a mouse button  \xC2\xB7  Esc cancels";
            const ImVec2 ts = text_size(F.medium, 13, t);
            dl->AddText(F.medium, px(13), ImVec2(x, p.y + (h - ts.y) / 2), kAccentHi, t);
        } else {
            const std::string v = bind_value(b);
            size_t at = 0;
            bool any = false;
            while (at < v.size()) {
                size_t e = v.find(' ', at);
                if (e == std::string::npos) e = v.size();
                const std::string k = v.substr(at, e - at);
                at = e + 1;
                if (k.empty() || k == "none") continue;
                any = true;
                const ImVec2 ts = text_size(F.semibold, 13, k.c_str());
                const ImVec2 c0(x, p.y + h / 2 - px(13)), c1(x + ts.x + px(20), p.y + h / 2 + px(13));
                dl->AddRectFilled(c0, c1, IM_COL32(255, 255, 255, 22), px(6));
                dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 30), px(6));
                dl->AddText(F.semibold, px(13), ImVec2(x + px(10), p.y + (h - ts.y) / 2), kText, k.c_str());
                x = c1.x + px(8);
            }
            if (!any) dl->AddText(F.regular, px(13), ImVec2(x, p.y + (h - ls.y) / 2), kFaint, "Not set");
        }
        /* Reset, when it is not the default */
        ImGui::SameLine(0, px(12));
        const bool custom = kv.count(b.key) != 0;
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!custom);
        const bool reset = hit("##reset", ImVec2(px(84), h));
        const bool rh = hot_item();
        ImGui::EndDisabled();
        if (custom) {
            const ImVec2 ts = text_size(F.semibold, 13, "Reset");
            dl->AddText(F.semibold, px(13), ImVec2(rp.x + (px(84) - ts.x) / 2, rp.y + (h - ts.y) / 2),
                        rh ? kText : kDim, "Reset");
        }
        if (reset && custom && cooldown == 0) {
            kv.erase(b.key);
            say(dod3::ini_write(ini, { { b.key, nullptr } }) ? "Saved" : "Could not save dod3.ini");
        }
        ImGui::PopID();
    }

    /* the key binding being set: the first key or button pressed */
    void poll_capture()
    {
        if (capture < 0) return;
        if (++capture_frames < 3) return;
        ImGuiIO& io = ImGui::GetIO();
        const char* name = nullptr;
        bool stop = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            stop = true;
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !name && !stop; k++)
            if (ImGui::IsKeyPressed((ImGuiKey)k, false)) name = key_name((ImGuiKey)k);
        static const char* const mouse[] = { "Mouse1", "Mouse2", "Mouse3", "Mouse4", "Mouse5" };
        for (int m = 0; m < 5 && !name && !stop; m++)
            if (ImGui::IsMouseClicked(m)) name = mouse[m];
        if (!name && !stop && io.MouseWheel > 0) name = "WheelUp";
        if (!name && !stop && io.MouseWheel < 0) name = "WheelDown";
        if (name) {
            const Bind& b = kBinds[capture];
            kv[b.key] = name;
            say(dod3::ini_write(ini, { { b.key, name } }) ? std::string(b.label) + ": " + name : "Could not save dod3.ini");
        }
        if (name || stop) {
            capture = -1;
            cooldown = 6;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        }
    }

    void settings_panel(ImVec2 body, ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        /* the tabs: L1 / R1 move between them too */
        if (capture < 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) tab = (tab + kTabs - 1) % kTabs;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) tab = (tab + 1) % kTabs;
        }
        float x = body.x;
        for (int t = 0; t < kTabs; t++) {
            const ImVec2 ts = text_size(F.semibold, 14, kTabNames[t]);
            const ImVec2 p(x, body.y), sz(ts.x + px(32), px(36));
            ImGui::SetCursorScreenPos(p);
            ImGui::PushID(t);
            if (hit("##tab", sz) && cooldown == 0 && capture < 0) tab = t;
            const bool hot = hot_item();
            ImGui::PopID();
            if (t == tab)
                dl->AddRectFilled(p, p + sz, IM_COL32(255, 255, 255, 30), px(18));
            else if (hot)
                dl->AddRectFilled(p, p + sz, IM_COL32(255, 255, 255, 12), px(18));
            dl->AddText(F.semibold, px(14), ImVec2(p.x + px(16), p.y + (sz.y - ts.y) / 2), t == tab ? kText : kDim,
                        kTabNames[t]);
            x += sz.x + px(6);
        }
        const float top = px(52);
        ImGui::SetCursorScreenPos(ImVec2(body.x, body.y + top));
        ImGui::BeginChild("##settings", ImVec2(size.x, size.y - top), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground);
        std::vector<const Dod3RowSpec*> list;
        for (const Dod3RowSpec& r : rows)
            if (tab_of(r.key) == tab && r.choices.size() > 1) list.push_back(&r);
        std::stable_sort(list.begin(), list.end(),
                         [](const Dod3RowSpec* a, const Dod3RowSpec* b) { return order_of(a->key) < order_of(b->key); });
        take_focus();
        for (const Dod3RowSpec* r : list) setting_row(*r);
        if (tab == kControls) {
            ImGui::Dummy(ImVec2(0, px(14)));
            ImGui::PushFont(F.semibold, 13);
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::TextUnformatted("KEYBOARD");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            para(F.regular, 13, kFaint,
                 "Click a row, then press the key or mouse button for it. The game's Controller Settings page says "
                 "what each button does.");
            ImGui::Dummy(ImVec2(0, px(6)));
            for (int i = 0; i < (int)(sizeof kBinds / sizeof *kBinds); i++) bind_row(i);
        }
        ImGui::Dummy(ImVec2(0, px(14)));
        if (button("Restore Defaults", 0, 0, capture < 0)) {
            std::vector<std::pair<std::string, const char*>> reset;
            for (const Dod3RowSpec* r : list) {
                const char* d = r->choices[r->def].first;
                if (d)
                    kv[r->key] = d;
                else
                    kv.erase(r->key);
                reset.push_back({ r->key, d });
            }
            if (tab == kControls)
                for (const Bind& b : kBinds) {
                    kv.erase(b.key);
                    reset.push_back({ b.key, nullptr });
                }
            say(dod3::ini_write(ini, reset) ? "Defaults restored" : "Could not save dod3.ini");
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::EndChild();
    }

    /* ---- the frame ---- */

    /* -1 to go on, else run_launcher's result */
    int frame()
    {
        frames++;
        ImGuiIO& io = ImGui::GetIO();
        W = io.DisplaySize.x;
        H = io.DisplaySize.y;
        s = H / 720.0f;
        dt = io.DeltaTime > 0 ? io.DeltaTime : 1.0f / 60;
        now = ImGui::GetTime();
        ImGuiStyle& st = ImGui::GetStyle();
        st.FontScaleMain = s / (st.FontScaleDpi > 0 ? st.FontScaleDpi : 1.0f);
        st.FrameRounding = px(10);
        st.ItemSpacing = ImVec2(px(8), px(6));
        if (cooldown > 0) cooldown--;
        if (test_key < test_keys.size() && frames > 40 && frames % 12 == 0) {   /* tests: down, then up */
            io.AddKeyEvent(test_keys[test_key], true);
        } else if (test_key < test_keys.size() && frames > 40 && frames % 12 == 1) {
            io.AddKeyEvent(test_keys[test_key++], false);
        }
        poll_checks();
        poll_capture();
        if (flow == Flow::Working && finished) {
            worker.join();
            have = installed(base);
            flow = ok ? Flow::Done : Flow::Failed;
            if (ok) say(!removing.empty() ? "Removed" : "Installed");
        }

        background();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoScrollWithMouse);
        title();
        int result = menu();
        const int f = footer();
        if (f >= 0) result = f;

        if (panel != Panel::None) {
            ImVec2 body, size;
            const char* heading = panel == Panel::Mods ? "Mods" : panel == Panel::Dlc ? "DLC" : "Settings";
            const char* sub = panel == Panel::Mods ? "Fan-made additions, installed beside your own game files."
                              : panel == Panel::Dlc ? "The downloadable content you own."
                                                    : "Saved to dod3.ini as you change them; used at the next start.";
            const bool close = panel_frame(heading, sub, &body, &size);
            if (panel == Panel::Settings)
                settings_panel(body, size);
            else if (flow != Flow::Idle)
                flow_page(body, size);
            else if (panel == Panel::Mods)
                mods_panel(body, size);
            else
                dlc_panel(body, size);
            const bool back = (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) &&
                              capture < 0 && cooldown == 0;
            if ((close || back) && flow != Flow::Working) {
                if (io.NavVisible) refocus = panel;
                panel = Panel::None;
                focus_panel = false;
            }
        }
        ImGui::End();
        toast_draw();

        /* tests: the window saved once it has settled */
        if (!grab.empty()) {
            if (frames == 50)
                if (FILE* g = fopen((grab + ".req").c_str(), "wb")) fclose(g);
            if (frames == 80 && grabbed < 0) {
                remove((grab + "_0.ppm").c_str());
                rename((grab + ".ppm").c_str(), (grab + "_0.ppm").c_str());
                grabbed = 0;
            }
        }
        if (auto_quit && frames >= 90 && flow != Flow::Working && checks.empty()) result = 1;
        if (auto_install && (flow == Flow::Done || flow == Flow::Failed)) {
            if (flow == Flow::Done)
                fprintf(stderr, "[launcher] done\n");
            else
                fprintf(stderr, "[launcher] failed: %s\n", error.c_str());
            result = 1;
        }
        return result;
    }
};

void load_fonts(Fonts& F)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   /* the arrays are the program's own */
    F.regular = io.Fonts->AddFontFromMemoryTTF((void*)kFont_Inter_Regular, (int)sizeof kFont_Inter_Regular, 17, &cfg);
    F.medium = io.Fonts->AddFontFromMemoryTTF((void*)kFont_Inter_Medium, (int)sizeof kFont_Inter_Medium, 17, &cfg);
    F.semibold = io.Fonts->AddFontFromMemoryTTF((void*)kFont_Inter_SemiBold, (int)sizeof kFont_Inter_SemiBold, 17, &cfg);
    F.display =
        io.Fonts->AddFontFromMemoryTTF((void*)kFont_InterDisplay_Bold, (int)sizeof kFont_InterDisplay_Bold, 17, &cfg);
    io.FontDefault = F.regular;
    ImGui::GetStyle().FontSizeBase = 17;
}

/* The disc's picture (the PSN release has the same file) carries the Dolby
 * and DTS logos and their trademark lines, which have no place in a
 * launcher: they are painted out. The logos, on black: every grey or white
 * pixel of their boxes; the two lines, partly over the dragon's tail: their
 * pure-white pixels. Each grown by a few pixels (the soft edges, the lines'
 * dark outline), then filled from the pixels around, inward. */
void clean_disc_art(std::vector<uint8_t>& px, int w, int h)
{
    struct Box {
        int x0, y0, x1, y1;
        bool white;
        int grow;
    };
    const Box boxes[] = { { 902, 716, 1172, 796, false, 1 },
                          { 902, 842, 1104, 923, false, 1 },
                          { 452, 962, 1332, 1030, true, 5 } };
    std::vector<uint8_t> mask((size_t)w * h);
    auto at = [&](int x, int y) { return &px[((size_t)y * w + x) * 4]; };
    for (const Box& b : boxes) {
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                const uint8_t* p = at(x, y);
                const int hi = std::max({ p[0], p[1], p[2] }), lo = std::min({ p[0], p[1], p[2] });
                if (b.white ? lo > 236 : (hi - lo < 48 && hi > 20)) mask[(size_t)y * w + x] = 1;
            }
        for (int g = 0; g < b.grow; g++) {
            std::vector<size_t> add;
            for (int y = std::max(1, b.y0 - b.grow); y < std::min(h - 1, b.y1 + b.grow); y++)
                for (int x = std::max(1, b.x0 - b.grow); x < std::min(w - 1, b.x1 + b.grow); x++) {
                    const size_t i = (size_t)y * w + x;
                    if (!mask[i] && (mask[i - 1] || mask[i + 1] || mask[i - w] || mask[i + w])) add.push_back(i);
                }
            for (size_t i : add) mask[i] = 1;
        }
    }
    std::vector<size_t> todo;
    for (size_t i = 0; i < mask.size(); i++)
        if (mask[i]) todo.push_back(i);
    while (!todo.empty()) {
        std::vector<size_t> next;
        std::vector<std::pair<size_t, uint32_t>> filled;
        for (size_t i : todo) {
            const int x = (int)(i % w), y = (int)(i / w);
            int acc[3] = { 0, 0, 0 }, n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h || mask[(size_t)ny * w + nx]) continue;
                    const uint8_t* p = at(nx, ny);
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    n++;
                }
            if (n >= 2)
                filled.push_back(
                    { i, (uint32_t)(acc[0] / n) | (uint32_t)(acc[1] / n) << 8 | (uint32_t)(acc[2] / n) << 16 });
            else
                next.push_back(i);
        }
        if (filled.empty()) break;
        for (const auto& f : filled) {
            uint8_t* p = &px[f.first * 4];
            p[0] = (uint8_t)f.second;
            p[1] = (uint8_t)(f.second >> 8);
            p[2] = (uint8_t)(f.second >> 16);
            mask[f.first] = 0;
        }
        todo.swap(next);
    }
}

/* the player's own key art, if the install has it */
ImTextureData* load_art(const fs::path& base)
{
    std::error_code ec;
    const fs::path p = base / "game/disc/PS3_GAME/PIC1.PNG";
    const uint64_t n = fs::file_size(p, ec);
    if (ec || n == 0 || n > (64u << 20)) return nullptr;
    std::vector<uint8_t> file((size_t)n);
    FILE* f = dod3::open_file(p, "rb");
    if (!f) return nullptr;
    const size_t got = fread(file.data(), 1, file.size(), f);
    fclose(f);
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (got != file.size() || !png_rgba(file, &rgba, &w, &h) || w <= 0 || h <= 0) return nullptr;
    if (n == 1181776 && w == 1920 && h == 1080) clean_disc_art(rgba, w, h);   /* the disc's own */
    ImTextureData* t = new ImTextureData();
    t->Create(ImTextureFormat_RGBA32, w, h);
    memcpy(t->Pixels, rgba.data(), rgba.size());
    t->UseColors = true;
    ImGui::RegisterUserTexture(t);
    return t;
}

void style()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(0, 0);
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.ScrollbarSize = 8;
    s.ScrollbarRounding = 8;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.035f, 0.043f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kText);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.22f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.30f);
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);   /* the items show their own focus */
}

}  // namespace

bool launcher_wanted(const fs::path& base, bool key_held)
{
    if (key_held) return true;
    if (const char* e = getenv("DOD3_LAUNCHER")) return strcmp(e, "0") != 0;
    const auto kv = dod3::ini_read(base / "dod3.ini");
    const auto it = kv.find("DOD3_LAUNCHER");
    return it == kv.end() || it->second != "0";
}

int run_launcher(const fs::path& base)
{
    if (!ui_init("Drakengard 3 Recompiled", 1280, 720, "", 17.0f)) return -1;
    style();
    Launcher L;
    L.base = base;
    L.ini = base / "dod3.ini";
    load_fonts(L.F);
    L.art = load_art(base);
    L.have = installed(base);
    L.kv = dod3::ini_read(L.ini);
    dod3_setting_rows(L.rows);
    extra_rows(L.rows);
    if (const char* g = getenv("DOD3_SETUP_GRAB")) L.grab = g;
    if (const char* a = getenv("DOD3_LAUNCHER_AUTO")) {
        L.auto_quit = !strcmp(a, "quit");
        L.auto_install = !strcmp(a, "install");
    }
    if (const char* o = getenv("DOD3_LAUNCHER_OPEN")) {
        L.panel = !strncmp(o, "mods", 4)       ? Panel::Mods
                  : !strncmp(o, "dlc", 3)      ? Panel::Dlc
                  : !strncmp(o, "settings", 8) ? Panel::Settings
                                               : Panel::None;
        if (const char* c = strchr(o, ':')) L.tab = std::clamp(atoi(c + 1), 0, kTabs - 1);
        L.panel_at = -10;
    }
    if (const char* f = getenv("DOD3_LAUNCHER_ADD")) {   /* '|'-separated */
        std::vector<fs::path> paths;
        for (const char* p = f; *p;) {
            const char* e = strchr(p, '|');
            const std::string one = e ? std::string(p, e) : std::string(p);
            if (!one.empty()) paths.push_back(fs::u8path(one));
            if (!e) break;
            p = e + 1;
        }
        L.add(paths);
    }
    if (const char* k = getenv("DOD3_LAUNCHER_KEYS")) {
        static const struct {
            const char* name;
            ImGuiKey key;
        } names[] = { { "Up", ImGuiKey_UpArrow },       { "Down", ImGuiKey_DownArrow }, { "Left", ImGuiKey_LeftArrow },
                      { "Right", ImGuiKey_RightArrow }, { "Space", ImGuiKey_Space },    { "Enter", ImGuiKey_Enter },
                      { "Esc", ImGuiKey_Escape },       { "Tab", ImGuiKey_Tab },        { "Q", ImGuiKey_Q },
                      { "L1", ImGuiKey_GamepadL1 },     { "R1", ImGuiKey_GamepadR1 } };
        for (const char* p = k; *p;) {
            const char* e = strchr(p, ',');
            const std::string one = e ? std::string(p, e) : std::string(p);
            for (const auto& n : names)
                if (one == n.name) L.test_keys.push_back(n.key);
            if (!e) break;
            p = e + 1;
        }
    }
    if (const char* r = getenv("DOD3_LAUNCHER_REMOVE")) {   /* spanish | undub */
        L.panel = Panel::Mods;
        L.start_remove(!strcmp(r, "undub") ? kUndub : kSpanish);
    }
    int result = 1;
    while (ui_frame_begin()) {
        const int r = L.frame();
        ui_frame_end();
        if (r >= 0) {
            result = r;
            break;
        }
    }
    /* the window was closed mid-install: stop the install first */
    if (L.worker.joinable()) {
        L.cancel = true;
        L.worker.join();
    }
    for (auto& c : L.checks) c.fut.wait();
    ui_shutdown();   /* the backend lets go of the picture's texture */
    delete L.art;
    return result;
}

}  // namespace dod3setup
