/*
 * The keyboard and mouse as the pad.
 *
 * The pad library asks this for port 0's state at every sweep
 * (cellPad_set_host_input), beside whatever real pad is there: keys and
 * mouse buttons held become the pad's buttons and the two sticks' directions
 * by a table of bindings, and the wheel a short press. The mouse's motion is
 * not here: it turns the camera directly (src/input/camera.cpp), with the
 * cursor hidden and kept in the window while the game has the focus.
 *
 * The bindings, each a list of keys (dod3.ini; "none" for no key):
 *
 *   KEY_CROSS      Space              KEY_CIRCLE    E, Backspace
 *   KEY_SQUARE     Mouse1             KEY_TRIANGLE  Mouse2
 *   KEY_L1         Q, Mouse3          KEY_R1        Shift
 *   KEY_L2         R                  KEY_R2        F
 *   KEY_L3         V, C               KEY_R3        V, X
 *   KEY_START      Enter              KEY_SELECT    Tab
 *   KEY_UP KEY_DOWN KEY_LEFT KEY_RIGHT              the arrows, and 1 3 4 2
 *   KEY_MOVE_UP _DOWN _LEFT _RIGHT                  W S A D (the left stick)
 *   KEY_LOOK_UP _DOWN _LEFT _RIGHT                  none none WheelUp WheelDown
 *                                                   (the right stick)
 *
 * Keys: A-Z, 0-9, F1-F12, Space, Enter, Tab, Esc, Backspace, Shift, Ctrl,
 * Alt, LShift RShift LCtrl RCtrl LAlt RAlt, Up Down Left Right, Insert
 * Delete Home End PageUp PageDown, CapsLock, Minus Equals LBracket RBracket
 * Semicolon Quote Comma Period Slash Backslash Grave, Num0-Num9, Mouse1
 * (left) Mouse2 (right) Mouse3 (middle) Mouse4 Mouse5, WheelUp WheelDown.
 *
 * V presses both sticks in (Intoner Mode). Escape is always the pad's back/
 * cancel button -- Circle, the runtime presses it for the window -- so it
 * closes a menu instead of the window (Alt+F4 does; the window's close button
 * too). With a pad plugged in
 * as well, either can be used at any moment: the prompts follow the one
 * touched last.
 *
 * The keys' state comes from SDL, kept updated as the Vulkan backend pumps
 * events; a key is only ever held while the game is in front.
 *
 *   DOD3_KEYBOARD=0   the toolkit's built-in keys instead (no mouse)
 *   DOD3_KBM_LOG=1    a line whenever what the keys hold changes
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "input/camera.h"
#include "input/kbm.h"
#include "rsx_host_mouse.h"
#include "util.h"

#include <SDL.h>
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

extern "C" {
/* ps3recomp/libs/input/cellPad.h */
typedef struct CellPadHostInput {
    uint16_t buttons;
    uint8_t lx, ly, rx, ry;
    uint8_t l2, r2;
} CellPadHostInput;
typedef int (*cellPad_host_input_fn)(void* user, int focused, CellPadHostInput* out);
void cellPad_set_host_input(cellPad_host_input_fn fn, void* user);
}

namespace {

const int kWheelUp = -1, kWheelDown = -2;   /* not keys: notches, each a short press */

/* SDL keeps its keyboard state updated as the Vulkan backend pumps events. */
const int kNoSuchKey = -3;
const int kMouseButton = 0x200;
const int kEitherShift = 0x300, kEitherCtrl = 0x301, kEitherAlt = 0x302;
int letter_key(char c) { return SDL_SCANCODE_A + c - 'A'; }
int function_key(int n) { return SDL_SCANCODE_F1 + n - 1; }
int numpad_key(int n)
{
    static const int keys[] = {
        SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
        SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7,
        SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
    };
    return keys[n];
}
bool key_held(int scancode)
{
    int count = 0;
    const Uint8* state = SDL_GetKeyboardState(&count);
    const auto key = [state, count](int code) {
        return state && code >= 0 && code < count && state[code] != 0;
    };
    if (scancode == kEitherShift)
        return key(SDL_SCANCODE_LSHIFT) || key(SDL_SCANCODE_RSHIFT);
    if (scancode == kEitherCtrl)
        return key(SDL_SCANCODE_LCTRL) || key(SDL_SCANCODE_RCTRL);
    if (scancode == kEitherAlt)
        return key(SDL_SCANCODE_LALT) || key(SDL_SCANCODE_RALT);
    if (scancode >= kMouseButton) {
        const Uint32 mask = SDL_GetMouseState(nullptr, nullptr);
        const int button = scancode - kMouseButton;
        static const Uint32 masks[] = {
            SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK,
            SDL_BUTTON_X1MASK, SDL_BUTTON_X2MASK,
        };
        return button >= 0 && button < (int)(sizeof masks / sizeof masks[0]) &&
               (mask & masks[button]) != 0;
    }
    return key(scancode);
}

const struct {
    const char* name;
    int vk;
} kKeys[] = {
    { "Space", SDL_SCANCODE_SPACE }, { "Enter", SDL_SCANCODE_RETURN },
    { "Tab", SDL_SCANCODE_TAB }, { "Esc", SDL_SCANCODE_ESCAPE },
    { "Backspace", SDL_SCANCODE_BACKSPACE }, { "Shift", kEitherShift },
    { "Ctrl", kEitherCtrl }, { "Alt", kEitherAlt },
    { "LShift", SDL_SCANCODE_LSHIFT }, { "RShift", SDL_SCANCODE_RSHIFT },
    { "LCtrl", SDL_SCANCODE_LCTRL }, { "RCtrl", SDL_SCANCODE_RCTRL },
    { "LAlt", SDL_SCANCODE_LALT }, { "RAlt", SDL_SCANCODE_RALT },
    { "Up", SDL_SCANCODE_UP }, { "Down", SDL_SCANCODE_DOWN },
    { "Left", SDL_SCANCODE_LEFT }, { "Right", SDL_SCANCODE_RIGHT },
    { "Insert", SDL_SCANCODE_INSERT }, { "Delete", SDL_SCANCODE_DELETE },
    { "Home", SDL_SCANCODE_HOME }, { "End", SDL_SCANCODE_END },
    { "PageUp", SDL_SCANCODE_PAGEUP }, { "PageDown", SDL_SCANCODE_PAGEDOWN },
    { "CapsLock", SDL_SCANCODE_CAPSLOCK }, { "Minus", SDL_SCANCODE_MINUS },
    { "Equals", SDL_SCANCODE_EQUALS }, { "LBracket", SDL_SCANCODE_LEFTBRACKET },
    { "RBracket", SDL_SCANCODE_RIGHTBRACKET }, { "Semicolon", SDL_SCANCODE_SEMICOLON },
    { "Quote", SDL_SCANCODE_APOSTROPHE }, { "Comma", SDL_SCANCODE_COMMA },
    { "Period", SDL_SCANCODE_PERIOD }, { "Slash", SDL_SCANCODE_SLASH },
    { "Backslash", SDL_SCANCODE_BACKSLASH }, { "Grave", SDL_SCANCODE_GRAVE },
    { "Mouse1", kMouseButton + 0 }, { "Mouse2", kMouseButton + 1 },
    { "Mouse3", kMouseButton + 2 }, { "Mouse4", kMouseButton + 3 },
    { "Mouse5", kMouseButton + 4 }, { "WheelUp", kWheelUp },
    { "WheelDown", kWheelDown },
};

const struct {
    const char* key;        /* dod3.ini */
    const char* defaults;
    uint16_t button;        /* CELL_PAD_CTRL_*, or 0 for a stick's direction */
} kBind[kBinds] = {
    { "KEY_CROSS", "Space", 1 << 14 },
    { "KEY_CIRCLE", "E Backspace", 1 << 13 },
    { "KEY_SQUARE", "Mouse1", 1 << 15 },
    { "KEY_TRIANGLE", "Mouse2", 1 << 12 },
    { "KEY_L1", "Q Mouse3", 1 << 10 },
    { "KEY_R1", "Shift", 1 << 11 },
    { "KEY_L2", "R", 1 << 8 },
    { "KEY_R2", "F", 1 << 9 },
    { "KEY_L3", "V C", 1 << 1 },
    { "KEY_R3", "V X", 1 << 2 },
    { "KEY_START", "Enter", 1 << 3 },
    { "KEY_SELECT", "Tab", 1 << 0 },
    { "KEY_UP", "Up 1", 1 << 4 },
    { "KEY_DOWN", "Down 3", 1 << 6 },
    { "KEY_LEFT", "Left 4", 1 << 7 },
    { "KEY_RIGHT", "Right 2", 1 << 5 },
    { "KEY_MOVE_UP", "W", 0 },
    { "KEY_MOVE_DOWN", "S", 0 },
    { "KEY_MOVE_LEFT", "A", 0 },
    { "KEY_MOVE_RIGHT", "D", 0 },
    { "KEY_LOOK_UP", "none", 0 },
    { "KEY_LOOK_DOWN", "none", 0 },
    { "KEY_LOOK_LEFT", "WheelUp", 0 },
    { "KEY_LOOK_RIGHT", "WheelDown", 0 },
};

struct Key {
    std::string name;   /* as the prompts name it */
    int vk;
};
std::vector<Key> s_keys[kBinds];
bool s_installed, s_mouse_look = true, s_log;
uint64_t s_wheel_until[2];   /* a notch's press, up and down, lasts until then */

bool lookup(const std::string& word, Key& out)
{
    if (word.size() == 1) {
        const char c = word[0];
        if (c >= 'a' && c <= 'z') {
            out = { std::string(1, (char)(c - 32)), letter_key((char)(c - 32)) };
            return true;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out = { word, letter_key(c) };
            return true;
        }
    }
    if ((word[0] == 'F' || word[0] == 'f') && word.size() <= 3) {
        const int n = atoi(word.c_str() + 1);
        if (n >= 1 && n <= 12) {
            out = { "F" + std::to_string(n), function_key(n) };
            return true;
        }
    }
    if (word.size() == 4 && !_strnicmp(word.c_str(), "Num", 3) && word[3] >= '0' && word[3] <= '9') {
        out = { std::string("Num") + word[3], numpad_key(word[3] - '0') };
        return true;
    }
    for (const auto& k : kKeys)
        if (!_stricmp(word.c_str(), k.name)) {
            out = { k.name, k.vk };
            return true;
        }
    return false;
}

void parse(int b, const char* list)
{
    s_keys[b].clear();
    std::string word;
    for (const char* p = list;; p++) {
        if (*p && *p != ',' && *p != ' ' && *p != '\t') {
            word += *p;
            continue;
        }
        if (!word.empty()) {
            Key k;
            if (!_stricmp(word.c_str(), "none")) {
                /* no key */
            } else if (lookup(word, k) && k.vk == kNoSuchKey) {
                fprintf(stderr, "[keyboard] %s: %s cannot be bound on this system\n", kBind[b].key, k.name.c_str());
            } else if (lookup(word, k)) {
                s_keys[b].push_back(k);
            } else {
                fprintf(stderr, "[keyboard] %s: no key called \"%s\"\n", kBind[b].key, word.c_str());
            }
            word.clear();
        }
        if (!*p) break;
    }
}

bool down(int b, uint64_t now)
{
    for (const Key& k : s_keys[b]) {
        if (k.vk == kWheelUp) {
            if (now < s_wheel_until[0]) return true;
        } else if (k.vk == kWheelDown) {
            if (now < s_wheel_until[1]) return true;
        } else if (key_held(k.vk))
            return true;
    }
    return false;
}

/* A stick from its four directions: a full push, the diagonals on the circle. */
void stick(bool up, bool dn, bool lf, bool rt, uint8_t& x, uint8_t& y)
{
    const int h = (rt ? 1 : 0) - (lf ? 1 : 0), v = (dn ? 1 : 0) - (up ? 1 : 0);
    const int reach = (h && v) ? 90 : 127;
    x = (uint8_t)(128 + h * reach);
    y = (uint8_t)(128 + v * reach);
}

int host_input(void*, int focused, CellPadHostInput* out)
{
    const uint64_t now = dod3_now_us();
    const int wheel = rsx_host_mouse_take_wheel();
    if (!focused) return 0;   /* keys held while another window is in front are not ours */
    if (wheel > 0) s_wheel_until[0] = now + 70000;
    if (wheel < 0) s_wheel_until[1] = now + 70000;

    bool on[kBinds];
    bool any = false;
    for (int b = 0; b < kBinds; b++) {
        on[b] = down(b, now);
        any = any || on[b];
        if (on[b]) out->buttons |= kBind[b].button;
    }
    stick(on[kBindMoveUp], on[kBindMoveDown], on[kBindMoveLeft], on[kBindMoveRight], out->lx, out->ly);
    stick(on[kBindLookUp], on[kBindLookDown], on[kBindLookLeft], on[kBindLookRight], out->rx, out->ry);
    if (on[kBindL2]) out->l2 = 255;
    if (on[kBindR2]) out->r2 = 255;
    if (s_log) {
        static CellPadHostInput last;
        if (memcmp(&last, out, sizeof last)) {
            last = *out;
            fprintf(stderr, "[keyboard] buttons %04X left %u,%u right %u,%u\n", out->buttons, out->lx, out->ly, out->rx,
                    out->ry);
        }
    }
    return any || now - dod3_camera_mouse_time() < 150000;
}

}  // namespace

/* A Steam Deck (its DMI board: Valve's Jupiter, or Galileo for the OLED). */
static bool on_steam_deck()
{
    auto read = [](const char* path) {
        std::string v;
        if (FILE* f = fopen(path, "r")) {
            char b[64] = {};
            if (fgets(b, sizeof b, f)) v = b;
            fclose(f);
        }
        while (!v.empty() && (v.back() == '\n' || v.back() == ' ')) v.pop_back();
        return v;
    };
    const std::string vendor = read("/sys/class/dmi/id/board_vendor"), product = read("/sys/class/dmi/id/product_name");
    return vendor == "Valve" && (product == "Jupiter" || product == "Galileo");
}

void dod3_kbm_install()
{
    if (const char* e = getenv("DOD3_KEYBOARD"))
        if (!strcmp(e, "0")) return;
    /* A Steam Deck, started outside Steam (Desktop Mode, from a terminal or
     * the file manager): Steam's Desktop Configuration turns the Deck's own
     * buttons into keys and its trackpad into the mouse, so every press came
     * twice -- once from the pad, once as Esc or Enter, which is Start -- and
     * the pause menu kept opening. The pad alone, then, unless DOD3_KEYBOARD=1
     * asks for the keys (a real keyboard plugged in). Started from Steam (a
     * non-Steam shortcut too), Steam gives the game a pad layout instead and
     * the keys are the player's. */
    if (!getenv("DOD3_KEYBOARD") && !getenv("SteamGameId") && !getenv("SteamAppId") && on_steam_deck()) {
        fprintf(stderr, "[kbm] Steam Deck outside Steam: keyboard and mouse off, the pad alone "
                        "(DOD3_KEYBOARD=1 for a keyboard of your own)\n");
        return;
    }
    for (int b = 0; b < kBinds; b++) {
        const char* e = getenv(kBind[b].key);
        parse(b, e && *e ? e : kBind[b].defaults);
    }
    if (const char* e = getenv("DOD3_MOUSE")) s_mouse_look = strcmp(e, "0") != 0;
    s_log = getenv("DOD3_KBM_LOG") != nullptr;
    s_installed = true;
    cellPad_set_host_input(host_input, nullptr);
    rsx_host_window_escape_closes(0);
    rsx_host_mouse_capture(s_mouse_look ? 1 : 0);
}

bool dod3_kbm_present() { return s_installed; }
bool dod3_kbm_mouse_look() { return s_installed && s_mouse_look; }
const char* dod3_kbm_key(Dod3Bind b) { return s_installed && !s_keys[b].empty() ? s_keys[b][0].name.c_str() : nullptr; }
