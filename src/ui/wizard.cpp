/* The installer's pages, shared by every platform (ui/ui.h has the window;
 * install/install.h does the work). In the spirit of Unleashed Recompiled's
 * installer: the player adds files -- any mix of the disc, the update, DLC
 * packages and mods, on any page -- each is recognised on its own, and a
 * light per item shows what is still needed. The keys for the game's executable
 * are checked as they are entered, by decrypting it.
 *
 *   Welcome -> Game files -> DLC (optional) -> Mods (optional) -> Keys (while
 *   the executable is still to be made) -> Install (space check) ->
 *   Installing -> Done / Failed
 *
 * It looks as the launcher does (ui/look.h): the title and the steps on the
 * left, the page in a panel on the right, over the game's key art -- the
 * install's, or on a first install the game's own as soon as it is added
 * (read from the disc or the PSN package, it fades in).
 *
 * Tests: DOD3_SETUP_ADD='<file>|<file>' adds files as Add Files would;
 * DOD3_SETUP_AUTO=<n> presses the primary button of the next n pages; with
 * DOD3_SETUP_GRAB=<prefix>, every page is saved as <prefix>_<n>.ppm (the
 * platform's grab: <prefix>.req -> <prefix>.ppm), DOD3_SETUP_GRAB_AT=<n>
 * frames after it opens (40). */
#include "ui/ui.h"
#include "ui/look.h"
#include "install/install.h"
#include "install/crypto.h"
#include "cpu/eboot.h"
#include "util.h"
#include "app_version.h"   /* generated: the port's version label */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

namespace dod3setup {

namespace {

using dod3::utf8;
using namespace look;

enum class Page { Welcome, GameFiles, Dlc, Mods, Keys, Confirm, Installing, Done, Failed };

struct Wizard : Kit {
    fs::path base;
    Page page = Page::Welcome;
    bool page_new = true;
    Status have;
    Plan plan;
    std::vector<std::string> notes;           /* what the last files added came to */
    struct Check {
        fs::path path;
        std::future<std::pair<Source, std::string>> fut;
    };
    std::vector<Check> checks;                /* files being recognised */

    /* the install, on its own thread */
    std::thread worker;
    std::atomic<bool> cancel{ false }, finished{ false };
    std::atomic<uint64_t> done{ 0 }, total{ 1 };
    std::mutex what_mu;
    std::string what, error;
    bool ok = false;
    std::chrono::steady_clock::time_point started;

    /* the Keys page: a field per key; a check (decrypting the executable)
     * runs whenever all of them are well-formed and have changed since */
    struct KeyBox {
        char text[160] = "";
    };
    std::vector<KeyBox> boxes = std::vector<KeyBox>(needed_keys().size());
    int key_gen = 0, checked_gen = -1, running_gen = -1;
    std::future<std::pair<int, std::string>> key_check;   /* 0 the keys work, 1 wrong keys, 2 another problem */
    int key_result = -1;
    std::string key_msg, key_note;

    /* the key art: the install's, else the added game's once it is read */
    ImTextureData* art = nullptr;
    double art_at = -10;
    struct Pixels {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        bool ok = false;
    };
    std::future<Pixels> art_job;
    bool art_loading = false;
    fs::path art_of;   /* the game source it was read from */

    /* tests (the header) */
    int page_frames = 0, auto_left = 0, grabs = 0, grab_at = 40;
    std::string grab;

    void go(Page p)
    {
        page = p;
        page_new = true;
        page_frames = 0;
    }

    void test_grab()
    {
        if (grab.empty()) return;
        if (page_frames == grab_at - 20) {
            if (FILE* f = fopen((grab + ".req").c_str(), "wb")) fclose(f);
        }
        if (page_frames == grab_at) {
            char n[16];
            snprintf(n, sizeof n, "_%d.ppm", grabs++);
            remove((grab + n).c_str());
            rename((grab + ".ppm").c_str(), (grab + n).c_str());
        }
    }

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
    }

    void poll_checks()
    {
        for (size_t i = 0; i < checks.size();) {
            if (checks[i].fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                i++;
                continue;
            }
            auto [s, err] = checks[i].fut.get();
            const std::string name =
                utf8(checks[i].path.filename().empty() ? checks[i].path : checks[i].path.filename());
            switch (s.kind) {
            case SourceKind::Disc: plan.disc = s; break;
            case SourceKind::Update: plan.update = s; break;
            case SourceKind::Eboot: plan.eboot = s; break;
            case SourceKind::Dlc: {
                auto& d = plan.dlc;
                d.erase(std::remove_if(d.begin(), d.end(), [&](const Source& o) { return o.id == s.id; }), d.end());
                d.push_back(s);
                break;
            }
            case SourceKind::Mod: {
                auto& m = plan.mods;
                m.erase(std::remove_if(m.begin(), m.end(), [&](const Source& o) { return o.id == s.id; }), m.end());
                m.push_back(s);
                break;
            }
            default: notes.push_back(name + ": " + err); break;
            }
            checks.erase(checks.begin() + (long)i);
        }
    }

    /* the game's art, read once the game is added (when the install has none) */
    void poll_art()
    {
        if (art_loading && art_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Pixels p = art_job.get();
            art_loading = false;
            if (p.ok && !art) {
                art = art_texture(p.rgba, p.w, p.h);
                art_at = now;
            }
        }
        if (art || art_loading || !plan.disc.set() || plan.disc.path == art_of) return;
        art_of = plan.disc.path;
        art_loading = true;
        const Source game = plan.disc;
        art_job = std::async(std::launch::async, [game] {
            Pixels p;
            std::vector<uint8_t> png;
            p.ok = key_art(game, &png) && art_pixels(png, &p.rgba, &p.w, &p.h);
            return p;
        });
    }

    bool base_ready() const
    { return (plan.disc.set() || have.disc) && (!update_required() || plan.update.set() || have.update); }
    /* the executable is still to be made; the player is asked for its keys
     * only when the setup has none built in */
    bool make_elf_needed() const { return !plan.eboot.set() && !have.eboot; }
    bool need_keys() const { return make_elf_needed() && !builtin_keys().complete(); }
    bool keys_ok() const { return key_result == 0 && checked_gen == key_gen; }
    bool eboot_source() const { return (update_required() ? plan.update.set() : plan.disc.set()) || have.eboot_bin; }

    Keys keys_from_boxes() const
    {
        Keys k;
        const auto& f = needed_keys();
        for (size_t i = 0; i < f.size(); i++) {
            std::vector<uint8_t> b;
            if (key_problem(f[i], boxes[i].text, &b).empty()) k.v[f[i].id] = b;
        }
        return k;
    }

    void fill_boxes(const Keys& k)
    {
        const auto& f = needed_keys();
        for (size_t i = 0; i < f.size(); i++) {
            auto it = k.v.find(f[i].id);
            if (it == k.v.end()) continue;
            snprintf(boxes[i].text, sizeof boxes[i].text, "%s", to_hex(it->second.data(), it->second.size()).c_str());
        }
        key_gen++;
    }

    /* start a check when the fields changed; collect one that finished */
    void poll_keys()
    {
        if (running_gen >= 0 && key_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto [r, m] = key_check.get();
            if (running_gen == key_gen) {
                key_result = r;
                key_msg = m;
                checked_gen = running_gen;
            }
            running_gen = -1;
        }
        if (running_gen >= 0 || checked_gen == key_gen || !need_keys() || !eboot_source()) return;
        const Keys k = keys_from_boxes();
        if (!k.complete()) {
            key_result = -1;
            return;
        }
        running_gen = key_gen;
        const Plan p = plan;
        const fs::path b = base;
        key_check = std::async(std::launch::async, [b, p, k] {
            std::vector<uint8_t> self, elf;
            std::string err;
            bool wrong = false;
            if (!read_eboot_bin(b, p, &self, &err)) return std::make_pair(2, err);
            if (!make_elf(self, k, &elf, &wrong, &err)) return std::make_pair(wrong ? 1 : 2, err);
            return std::make_pair(0, std::string());
        });
    }
    bool anything() const
    {
        return plan.disc.set() || plan.update.set() || plan.eboot.set() || !plan.dlc.empty() || !plan.mods.empty();
    }

    /* ---- pieces ---------------------------------------------------------- */

    void add_files(bool folder)
    {
        add(ui_pick_files(folder ? "Add Drakengard 3 files" : "Add DLC packages",
                          { { "Drakengard 3 files (.iso, .pkg, .zip)", "*.iso;*.pkg;*.zip" }, { "All files", "*.*" } }));
    }
    void add_folder()
    {
        const fs::path p = ui_pick_folder("The folder that holds PS3_GAME, or a mod's folder");
        if (!p.empty()) add({ p });
    }

    /* what is being checked, and what the last files came to */
    void status_lines()
    {
        if (!checks.empty()) {
            static const char* dots[] = { "", ".", "..", "..." };
            char t[64];
            snprintf(t, sizeof t, "Checking %zu file%s%s", checks.size(), checks.size() == 1 ? "" : "s",
                     dots[(int)(now * 3) % 4]);
            para(F.regular, 14, kDim, t);
        }
        for (const std::string& n : notes) para(F.regular, 14, kErr, n.c_str());
    }

    void adding_line(uint64_t bytes)
    {
        if (!bytes) return;
        para(F.regular, 13, kFaint, ("Adding " + gb(bytes)).c_str());
        ImGui::Dummy(ImVec2(0, px(4)));
    }

    /* a card's title with a pill at its right */
    void card_title(const char* t, const char* pill_text, ImU32 pill_col)
    {
        ImGui::PushFont(F.semibold, 18);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
        const ImVec2 tp = ImGui::GetItemRectMin();
        pill(ImGui::GetWindowDrawList(), ImVec2(card_x1 - px(18), tp.y + px(2)), pill_text, pill_col);
    }

    /* one line of a list: a mark, a name, a note after it */
    enum Mark { kNone, kTick, kDot, kRing };
    void list_row(Mark m, ImU32 col, const char* name, ImU32 name_col, const char* after = nullptr,
                  ImU32 after_col = kFaint, ImFont* font = nullptr, float size = 14)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c = ImGui::GetCursorScreenPos();
        ImGui::PushFont(font ? font : F.regular, size);
        const float lh = ImGui::GetTextLineHeight();
        const ImVec2 mc(c.x + px(6), c.y + lh / 2);
        if (m == kTick)
            tick(dl, mc, col);
        else if (m == kDot)
            dot(dl, mc, col, true);
        else if (m == kRing)
            dot(dl, mc, col, false);
        ImGui::SetCursorScreenPos(ImVec2(c.x + px(22), c.y));
        ImGui::PushStyleColor(ImGuiCol_Text, name_col);
        ImGui::TextUnformatted(name);
        ImGui::PopStyleColor();
        if (after && *after) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, after_col);
            ImGui::TextUnformatted(after);
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
    }

    /* ---- the pages ------------------------------------------------------ */

    /* the Welcome page's list: what is needed, and what of it there is */
    void need_row(const char* name, const char* about, const char* state, ImU32 col)
    {
        const float wr = wrap_right;
        wrap_right = wr - text_size(F.semibold, 12, state).x - px(34);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        para(F.semibold, 15, kText, name);
        wrap_right = wr;
        para(F.regular, 13, kDim, about);
        pill(ImGui::GetWindowDrawList(), ImVec2(card_x1 - px(18), p.y), state, col);
        ImGui::Dummy(ImVec2(0, px(7)));
    }

    void page_welcome()
    {
        card_begin();
        auto state = [&](bool added, bool installed, bool required, const char** t, ImU32* c) {
            *t = added ? "Added" : installed ? "Installed" : required ? "Required" : "Optional";
            *c = added || installed ? kOk : required ? kWarn : kFaint;
        };
        const char* t;
        ImU32 c;
        state(plan.disc.set(), have.disc, true, &t, &c);
        need_row("The game",
                 update_required() ? "The disc, as a decrypted .iso or the folder holding PS3_GAME; or the US PSN "
                                     "package (.pkg)."
                                   : "The disc, as a decrypted .iso or the folder holding PS3_GAME.",
                 t, c);
        if (update_required()) {
            state(plan.update.set(), have.update, true, &t, &c);
            need_row("Update 1.01", "The update package, UP0082-BLUS31197_00-DOD3PATCH0000000 (.pkg).", t, c);
        }
        state(!plan.dlc.empty(), !have.dlc.empty(), false, &t, &c);
        need_row("DLC", "The DLC packages (.pkg) you own. No licence files are needed.", t, c);
        if (update_required()) {
            state(!plan.mods.empty(), !have.mods.empty(), false, &t, &c);
            need_row("Mods", "Fan-made additions: the Spanish translation, the DLC Undub.", t, c);
        }
        if (need_keys()) {
            state(keys_ok(), false, true, &t, &c);
            need_row("Keys",
                     update_required() ? "The keys that open the update's EBOOT.BIN, the game's executable."
                                       : "The keys that open the disc's EBOOT.BIN, the game's executable.",
                     t, c);
        }
        card_end();
        para(F.regular, 13, kDim,
             have.ready() ? "Add files on the next pages to replace any of the game's, or to add DLC."
                          : "Files can be added on any page; each one is recognised by itself. Nothing is changed "
                            "in the files you add.");
        ImGui::Dummy(ImVec2(0, px(2)));
        path_line(F.regular, 13, kFaint, have.ready() ? "Installed in " : "Installing to ", base);
    }

    void game_card(const char* title, const char* id_line, const char* need, const Source& src, bool installed,
                   const char* kind)
    {
        card_begin();
        card_title(title, src.set() ? "Added" : installed ? "Installed" : "Needed",
                   src.set() || installed ? kOk : kWarn);
        para(F.medium, 13, kDim, id_line);
        ImGui::Dummy(ImVec2(0, px(4)));
        if (src.set()) {
            path_line(F.regular, 13, alpha(kText, 0.82f), "", src.path);
            para(F.regular, 13, kFaint, (std::string(kind) + " \xC2\xB7 " + gb(src.bytes)).c_str());
        } else if (installed) {
            para(F.regular, 14, alpha(kText, 0.82f), "Installed. Add it again to replace it.");
        } else {
            para(F.regular, 14, alpha(kText, 0.82f), need);
        }
        card_end();
    }

    void page_game_files()
    {
        std::error_code ec;
        const bool folder = fs::is_directory(plan.disc.path, ec);
        game_card("Game",
                  update_required() ? "The disc (BLUS31197) or the US PSN package (NPUB31251)" : "The disc (BLUS31197)",
                  update_required()
                      ? "Add a decrypted .iso or the PSN package (UP0082-NPUB31251_00-DOD3MAINGAME0000) with Add "
                        "Files, or the folder holding PS3_GAME with Add Folder."
                      : "Add a decrypted .iso with Add Files, or the folder holding PS3_GAME with Add Folder.",
                  plan.disc, have.disc,
                  plan.disc.psn ? "PSN package" : folder ? "Disc folder" : "Disc image");
        if (update_required())
            game_card("Update 1.01", "UP0082-BLUS31197_00-DOD3PATCH0000000",
                      "Add the update package (.pkg) with Add Files: the disc release's, which the PSN release takes "
                      "too.",
                      plan.update, have.update, "Package");
        status_lines();
    }

    void page_dlc()
    {
        struct Group {
            const char* title;
            const char* prefix;
        };
        const Group groups[] = { { "STORY", "ADDSCENARIO" },
                                 { "VOICES", "ADDJAPANESEVOICE" },
                                 { "MUSIC", "ADDSONG" },
                                 { "OUTFITS AND WEAPONS", "ADDWEAPONCLOTHE" } };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cw = (ImGui::GetContentRegionAvail().x - px(12)) / 2;
        uint64_t adding = 0;
        for (const Source& x : plan.dlc) adding += x.bytes;
        adding_line(adding);
        status_lines();
        for (const Group& g : groups) {
            ImGui::PushFont(F.semibold, 12);
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::TextUnformatted(g.title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
            int col = 0;
            for (const DlcInfo& d : known_dlc()) {
                if (strncmp(d.id, g.prefix, strlen(g.prefix))) continue;
                bool added = false;
                for (const Source& x : plan.dlc) added |= x.id == d.id;
                const bool in = have.dlc.count(d.id) != 0;
                if (col) ImGui::SameLine(0, px(12));
                const ImVec2 p = ImGui::GetCursorScreenPos(), sz(cw, px(44));
                ImGui::Dummy(sz);
                dl->AddRectFilled(p, p + sz, added ? alpha(kOk, 0.10f) : in ? IM_COL32(255, 255, 255, 12)
                                                                            : IM_COL32(255, 255, 255, 5),
                                  px(10));
                if (added) dl->AddRect(p, p + sz, alpha(kOk, 0.35f), px(10));
                if (added || in)
                    tick(dl, ImVec2(p.x + px(18), p.y + sz.y / 2), kOk);
                else
                    dot(dl, ImVec2(p.x + px(18), p.y + sz.y / 2), kFaint, false);
                const char* state = added ? "Added" : in ? "Installed" : nullptr;
                const float sw = state ? text_size(F.semibold, 12, state).x + px(14) : 0;
                const ImVec2 ts = text_size(F.medium, 14, d.name);
                dl->AddText(F.medium, px(14), ImVec2(p.x + px(36), p.y + (sz.y - ts.y) / 2), added || in ? kText : kDim,
                            d.name, nullptr);
                if (state) {
                    const ImVec2 ss = text_size(F.semibold, 12, state);
                    dl->AddText(F.semibold, px(12), ImVec2(p.x + sz.x - sw, p.y + (sz.y - ss.y) / 2),
                                added ? kOk : kFaint, state);
                }
                col ^= 1;
            }
            ImGui::Dummy(ImVec2(0, px(10)));
        }
    }

    void mod_card(const ModGroup& g)
    {
        int added = 0, in = 0;
        auto is_added = [&](const char* id) {
            for (const Source& m : plan.mods)
                if (m.id == id) return true;
            return false;
        };
        auto is_in = [&](const char* id) {
            for (const auto& m : have.mods)
                if (m.first == id) return true;
            return false;
        };
        for (const ModPart& p : g.parts) {
            added += is_added(p.id);
            in += is_in(p.id);
        }
        const int n = (int)g.parts.size();
        bool off = in > 0;   /* switched off in the launcher */
        for (const ModPart& p : g.parts)
            if (is_in(p.id) && mod_enabled(base, p.id)) off = false;
        card_begin();
        if (added)
            card_title(g.title, "Added", kOk);
        else if (off)
            card_title(g.title, "Switched off", kFaint);
        else if (in == n)
            card_title(g.title, "Installed", kOk);
        else if (in > 0)
            card_title(g.title, "Partly installed", kWarn);
        else
            card_title(g.title, "Not added", kFaint);
        para(F.medium, 13, kDim, g.by);
        ImGui::Dummy(ImVec2(0, px(4)));
        para(F.regular, 14, alpha(kText, 0.82f), g.about);
        if (g.parts.size() > 1) {
            ImGui::Dummy(ImVec2(0, px(4)));
            for (const ModPart& p : g.parts) {
                const bool a = is_added(p.id), i = is_in(p.id);
                std::string after = a ? "added" : i ? "installed" : "";
                if (!a && !i && p.needs) after = p.needs;
                list_row(a || i ? kTick : kRing, a || i ? kOk : kFaint, p.name, a || i ? kText : kDim, after.c_str(),
                         a ? kOk : kFaint);
            }
        }
        if (!strcmp(g.title, kUndub.title) && needs_voice_pack(plan, have)) {
            ImGui::Dummy(ImVec2(0, px(4)));
            para(F.medium, 13, kWarn, "Needs the Japanese Voice Pack: add its package on the DLC page too.");
        }
        card_end();
    }

    void page_mods()
    {
        uint64_t adding = 0;
        for (const Source& x : plan.mods) adding += x.bytes;
        adding_line(adding);
        status_lines();
        mod_card(kSpanish);
        mod_card(kUndub);
        /* others: added, or installed before */
        auto known = [&](const std::string& id) {
            for (const ModGroup* g : { &kSpanish, &kUndub })
                for (const ModPart& p : g->parts)
                    if (id == p.id) return true;
            return false;
        };
        for (const Source& m : plan.mods)
            if (!known(m.id)) {
                card_begin();
                card_title(m.name.c_str(), "Added", kOk);
                para(F.regular, 13, kDim, "A homebrew package laid out as the console installs it.");
                card_end();
            }
        for (const auto& m : have.mods) {
            bool listed = known(m.first);
            for (const Source& x : plan.mods) listed |= x.id == m.first;
            if (listed) continue;
            card_begin();
            card_title(m.second.c_str(), "Installed", kOk);
            card_end();
        }
        if (!have.mods.empty())
            para(F.regular, 13, kFaint, "Installed mods are taken out with Remove, in the launcher's Mods page.");
    }

    void page_keys()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        para(F.regular, 14, alpha(kText, 0.82f),
             update_required()
                 ? "The game's executable is made from the update's EBOOT.BIN, which is encrypted. Enter the keys "
                   "that open it, as hex. They are checked by decrypting it, and kept in this install for next time."
                 : "The game's executable is made from the disc's EBOOT.BIN, which is encrypted. Enter the keys "
                   "that open it, as hex. They are checked by decrypting it, and kept in this install for next time.");
        ImGui::Dummy(ImVec2(0, px(8)));
        const auto& f = needed_keys();
        for (size_t i = 0; i < f.size(); i++) {
            std::vector<uint8_t> b;
            const bool empty = boxes[i].text[0] == 0;
            const std::string problem = empty ? "" : key_problem(f[i], boxes[i].text, &b);
            char bytes[32];
            snprintf(bytes, sizeof bytes, "%zu bytes", f[i].bytes);
            const ImVec2 c = ImGui::GetCursorScreenPos();
            list_row(kNone, 0, f[i].label, kText, bytes, kFaint, F.semibold, 15);
            if (!empty) {
                const float right = c.x + ImGui::GetContentRegionAvail().x - px(8);
                const float lh = ImGui::GetItemRectSize().y;
                if (problem.empty())
                    tick(dl, ImVec2(right, c.y + lh / 2), kOk);
                else
                    dot(dl, ImVec2(right, c.y + lh / 2), kWarn, true);
            }
            char id[16];
            snprintf(id, sizeof id, "##key%zu", i);
            ImGui::PushFont(F.medium, 15);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText(id, boxes[i].text, sizeof boxes[i].text)) key_gen++;
            ImGui::PopFont();
            if (!problem.empty()) para(F.regular, 13, kErr, problem.c_str());
            ImGui::Dummy(ImVec2(0, px(8)));
        }
        if (button("Load Key File...", 0, 0)) {
            const auto picked = ui_pick_files("A key file (name=hex lines)",
                                              { { "Key files (.txt)", "*.txt" }, { "All files", "*.*" } });
            if (!picked.empty()) {
                Keys k;
                std::string err;
                if (keys_load(picked[0], &k, &err)) {
                    fill_boxes(k);
                    key_note = std::to_string(k.v.size()) + " of " + std::to_string(f.size()) + " keys found in " +
                               utf8(picked[0].filename());
                } else {
                    key_note = utf8(picked[0].filename()) + ": " + err;
                }
            }
        }
        if (!key_note.empty()) {
            const ImVec2 bmax = ImGui::GetItemRectMax(), bmin = ImGui::GetItemRectMin();
            const ImVec2 ts = text_size(F.regular, 13, key_note.c_str());
            dl->AddText(F.regular, px(13), ImVec2(bmax.x + px(14), bmin.y + (bmax.y - bmin.y - ts.y) / 2), kDim,
                        key_note.c_str());
        }
        ImGui::Dummy(ImVec2(0, px(8)));
        if (!eboot_source()) {
            para(F.regular, 14, kDim,
                 update_required() ? "Add the update package on the Game Files page: the executable is made from it."
                                   : "Add the disc on the Game Files page: the executable is made from it.");
        } else if (running_gen >= 0) {
            para(F.regular, 14, kDim, "Checking the keys...");
        } else if (keys_ok()) {
            list_row(kTick, kOk, "These keys open the game's executable.", kText, nullptr, kFaint, F.medium, 14);
        } else if (key_result > 0 && checked_gen == key_gen) {
            para(F.medium, 14, kErr,
                 key_result == 1 ? "These keys do not open EBOOT.BIN. Check each one against your key set (key "
                                   "revision 0x1C)."
                                 : key_msg.c_str());
        }
    }

    void page_confirm()
    {
        const uint64_t need = plan.bytes(), space = free_bytes(base);
        if (!anything() && !make_elf_needed()) {
            para(F.regular, 14, kDim,
                 have.ready() ? "Everything added is installed already." : "Add the game's files first.");
            return;
        }
        auto row = [&](const std::string& name, uint64_t bytes) {
            list_row(kDot, kOk, name.c_str(), kText, bytes ? gb(bytes).c_str() : nullptr, kFaint, F.medium, 15);
            ImGui::Dummy(ImVec2(0, px(2)));
        };
        card_begin();
        if (plan.disc.set()) row(plan.disc.psn ? "Game (PSN package)" : "Game disc", plan.disc.bytes);
        if (plan.update.set()) row("Update 1.01", plan.update.bytes);
        if (plan.eboot.set())
            row("EBOOT.ELF", plan.eboot.bytes);
        else if (make_elf_needed())
            row(need_keys()         ? "The game's executable, made with your keys"
                : update_required() ? "The game's executable, made from the update"
                                    : "The game's executable, made from the disc",
                update_required() ? 26872424u : 26872552u);
        for (const Source& d : plan.dlc) row(d.name, d.bytes);
        for (const Source& m : plan.mods) row(m.name, m.bytes);
        card_end();
        char t[96];
        snprintf(t, sizeof t, "Needs %s; %s free on the drive.", gb(need).c_str(), gb(space).c_str());
        para(F.regular, 14, kDim, t);
        path_line(F.regular, 13, kFaint, "To ", base);
        if (space && need > space) {
            ImGui::Dummy(ImVec2(0, px(4)));
            char e[128];
            snprintf(e, sizeof e, "Not enough space. Free %s, or install fewer DLC packs.", gb(need - space).c_str());
            para(F.medium, 14, kErr, e);
        }
    }

    void page_installing()
    {
        std::string w;
        {
            std::lock_guard<std::mutex> g(what_mu);
            w = what;
        }
        ImGui::Dummy(ImVec2(0, px(10)));
        ImGui::PushFont(F.semibold, 19);
        ImGui::TextUnformatted(w.empty() ? "Starting..." : w.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, px(14)));
        const uint64_t d = done, t = total ? total.load() : 1;
        progress((float)((double)d / (double)t));
        ImGui::Dummy(ImVec2(0, px(8)));
        if (!w.empty()) {
            std::string line = gb(d) + " of " + gb(t);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if (secs > 3 && d > 0 && d < t) {
                const double rate = d / secs, left = (t - d) / rate;
                char r[96];
                snprintf(r, sizeof r, "  \xC2\xB7  %.0f MB/s  \xC2\xB7  about %d min %02d s left", rate / 1048576.0,
                         (int)left / 60, (int)left % 60);
                line += r;
            }
            para(F.regular, 13, kDim, line.c_str());
        }
        if (cancel) para(F.regular, 13, kWarn, "Cancelling...");
    }

    void page_done()
    {
        card_begin();
        list_row(kTick, kOk, "The game", kText, nullptr, kFaint, F.medium, 15);
        ImGui::Dummy(ImVec2(0, px(2)));
        if (update_required()) {
            list_row(kTick, kOk, "Update 1.01", kText, nullptr, kFaint, F.medium, 15);
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        char dlc[64];
        snprintf(dlc, sizeof dlc, "%zu of %zu", have.dlc.size(), known_dlc().size());
        list_row(have.dlc.empty() ? kRing : kTick, have.dlc.empty() ? kFaint : kOk, "DLC", kText, dlc, kFaint,
                 F.medium, 15);
        ImGui::Dummy(ImVec2(0, px(2)));
        if (update_required()) {
            std::string mods;
            for (const auto& m : have.mods) mods += (mods.empty() ? "" : ", ") + m.second;
            list_row(have.mods.empty() ? kRing : kTick, have.mods.empty() ? kFaint : kOk, "Mods", kText,
                     have.mods.empty() ? "none" : nullptr, kFaint, F.medium, 15);
            if (!mods.empty()) {
                ImGui::Indent(px(22));
                para(F.regular, 13, kDim, mods.c_str());
                ImGui::Unindent(px(22));
            }
        }
        card_end();
        para(F.regular, 14, alpha(kText, 0.82f),
             "From now on the game opens on its launcher: Play, and the mods, the DLC and the settings.");
    }

    void page_failed()
    {
        if (!cancel) {
            para(F.medium, 14, kErr, error.c_str());
            ImGui::Dummy(ImVec2(0, px(6)));
        }
        para(F.regular, 14, kDim, "The part that was being installed has been taken out again; the parts before it "
                                  "stay.");
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
        started = std::chrono::steady_clock::now();
        plan.keys = builtin_keys().complete() ? builtin_keys() : keys_from_boxes();
        const Plan p = plan;
        worker = std::thread([this, p] {
            std::string err;
            const bool r = install(
                base, p,
                [this](uint64_t d, uint64_t t, const std::string& w) {
                    done = d;
                    total = t ? t : 1;
                    {
                        std::lock_guard<std::mutex> g(what_mu);
                        what = w;
                    }
                    return !cancel.load();
                },
                &err);
            ok = r;
            error = err;
            finished = true;
        });
        go(Page::Installing);
    }

    /* ---- the steps, on the left ------------------------------------------ */

    std::string step_note(Page p) const
    {
        char t[64];
        switch (p) {
        case Page::GameFiles:
            if (!plan.disc.set() && !have.disc) return "The game is needed";
            if (update_required() && !plan.update.set() && !have.update) return "The update is needed";
            return plan.disc.set() || plan.update.set() ? "Added" : "Installed";
        case Page::Dlc:
            if (!plan.dlc.empty()) {
                snprintf(t, sizeof t, "%zu added", plan.dlc.size());
                return t;
            }
            if (!have.dlc.empty()) {
                snprintf(t, sizeof t, "%zu of %zu installed", have.dlc.size(), known_dlc().size());
                return t;
            }
            return "Optional";
        case Page::Mods:
            if (!plan.mods.empty()) {
                snprintf(t, sizeof t, "%zu added", plan.mods.size());
                return t;
            }
            if (!have.mods.empty()) {
                snprintf(t, sizeof t, "%zu installed", have.mods.size());
                return t;
            }
            return "Optional";
        case Page::Keys: return keys_ok() ? "Checked" : "Needed for the executable";
        case Page::Confirm:
            if (page == Page::Installing) {
                const uint64_t d = done, n = total ? total.load() : 1;
                snprintf(t, sizeof t, "%d%%", (int)(100.0 * (double)d / (double)n));
                return t;
            }
            return page == Page::Done ? "Done" : page == Page::Failed ? (cancel ? "Cancelled" : "Failed") : "";
        default: return "";
        }
    }

    void steps()
    {
        struct Step {
            const char* label;
            Page page;
        };
        std::vector<Step> list = { { "Welcome", Page::Welcome }, { "Game Files", Page::GameFiles }, { "DLC", Page::Dlc } };
        if (update_required()) list.push_back({ "Mods", Page::Mods });   /* mods are for the 1.01 build */
        if (need_keys() || page == Page::Keys) list.push_back({ "Keys", Page::Keys });
        list.push_back({ "Install", Page::Confirm });
        const Page shown = page == Page::Installing || page == Page::Done || page == Page::Failed ? Page::Confirm : page;
        int at = 0;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].page == shown) at = (int)i;
        const bool locked = page == Page::Installing || page == Page::Done || page == Page::Failed;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x = px(72), step = px(58), r = px(12);
        float y = px(236);
        for (int i = 0; i < (int)list.size(); i++) {
            const bool cur = i == at, past = i < at || page == Page::Done;
            const bool can = i < at && !locked;   /* back to an earlier step */
            ImGui::SetCursorScreenPos(ImVec2(x - px(10), y - px(8)));
            ImGui::PushID(i);
            ImGui::BeginDisabled(!can);
            const bool pressed = hit("##step", ImVec2(px(300), px(50))) && cooldown == 0;
            const bool hot = can && hot_item();
            ImGui::EndDisabled();
            ImGui::PopID();
            float& a = anim_of(ImGui::GetItemID());
            approach(a, hot ? 1.0f : 0.0f, 16.0f);
            if (pressed && can) go(list[i].page);

            const ImVec2 c(x + r, y + r);
            if (i + 1 < (int)list.size())
                dl->AddLine(ImVec2(c.x, c.y + r + px(5)), ImVec2(c.x, c.y + step - r - px(5)),
                            past && i + 1 <= at ? alpha(kOk, 0.35f) : kLine, px(1.5f));
            char num[8];
            snprintf(num, sizeof num, "%d", i + 1);
            const ImVec2 ns = text_size(F.semibold, 12, num);
            if (cur && page != Page::Done) {
                dl->AddCircleFilled(c, r, page == Page::Failed ? kErr : kAccent);
                dl->AddText(F.semibold, px(12), ImVec2(c.x - ns.x / 2, c.y - ns.y / 2), kText, num);
            } else if (past) {
                dl->AddCircleFilled(c, r, alpha(kOk, 0.14f + 0.10f * a));
                tick(dl, c, kOk);
            } else {
                dl->AddCircle(c, r, alpha(kText, 0.22f), 0, px(1.4f));
                dl->AddText(F.semibold, px(12), ImVec2(c.x - ns.x / 2, c.y - ns.y / 2), kFaint, num);
            }
            const ImU32 col = cur ? kText : past ? mix(alpha(kText, 0.72f), kText, a) : alpha(kText, 0.42f);
            const float lx = x + r * 2 + px(16) + px(6) * a;
            dl->AddText(F.medium, px(19), ImVec2(lx, y + px(1)), col, list[i].label);
            const std::string note = step_note(list[i].page);
            if (!note.empty())
                dl->AddText(F.regular, px(13), ImVec2(lx, y + px(25)), cur ? kDim : alpha(kDim, 0.7f), note.c_str());
            y += step;
        }
        char v[64];
        snprintf(v, sizeof v, "Version %s  \xC2\xB7  Setup", DOD3_VERSION_STRING);
        dl->AddText(F.regular, px(13), ImVec2(px(72), H - px(52)), alpha(kDim, 0.8f), v);
    }

    /* ---- the foot: what a page adds on the left, the way on at the right -- */

    struct Btn {
        const char* label;
        bool primary;   /* the accent; Enter or a pad's confirm button when nothing shows the focus */
        bool enabled;
        bool back;      /* Esc or a pad's back button */
    };

    /* returns the one pressed, or -1 */
    int nav(float y, float right, const std::vector<Btn>& bs)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float gap = px(10);
        std::vector<float> w;
        float span = -gap;
        for (const Btn& b : bs) {
            w.push_back(std::max(b.primary ? 136.0f : 104.0f, text_size(F.semibold, 15, b.label).x / s + 36));
            span += px(w.back()) + gap;
        }
        float x = right - span;
        int pressed = -1, primary = -1, back = -1;
        for (size_t i = 0; i < bs.size(); i++) {
            const Btn& b = bs[i];
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (b.primary) {
                primary = (int)i;
                if (page_new && io.NavVisible) ImGui::SetKeyboardFocusHere();
            }
            if (b.back) back = (int)i;
            if (button(b.label, w[i], b.primary ? 1 : 0, b.enabled)) pressed = (int)i;
            x += px(w[i]) + gap;
        }
        if (pressed < 0 && primary >= 0 && bs[primary].enabled) {
            if (auto_left > 0 && page_frames >= std::max(45, grab_at + 5)) {
                auto_left--;
                pressed = primary;
            } else if (!io.NavVisible && cooldown == 0 && checks.empty() &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)))
                pressed = primary;
        }
        if (pressed < 0 && back >= 0 && bs[back].enabled && cooldown == 0 && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
            pressed = back;
        return pressed;
    }

    /* returns -1 to go on, else run_installer's result */
    int foot(ImVec2 body, ImVec2 size)
    {
        const float y = body.y + size.y - px(44), right = body.x + size.x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(body.x, y - px(16)), ImVec2(right, y - px(16)), kLine);
        ImGui::SetCursorScreenPos(ImVec2(body.x, y));
        const bool busy = !checks.empty();
        /* adding, on the pages that take files */
        if (page == Page::GameFiles || page == Page::Mods) {
            if (button("Add Files...", 0, page == Page::GameFiles && !base_ready() ? 1 : 0)) add_files(true);
            ImGui::SameLine(0, px(10));
            if (button("Add Folder...", 0, 0)) add_folder();
        } else if (page == Page::Dlc) {
            if (button("Add DLC Packages...", 0, 0)) add_files(false);
        } else if (page == Page::Done) {
            if (button("Add More", 0, 0)) go(Page::Dlc);
        }
        int result = -1;
        switch (page) {
        case Page::Welcome:
            if (have.ready()) {
                switch (nav(y, right, { { "Close", false, true, false }, { "Play", false, true, false },
                                        { "Next", true, true, false } })) {
                case 0: result = 1; break;
                case 1: result = 0; break;
                case 2: go(Page::GameFiles); break;
                }
            } else {
                switch (nav(y, right, { { "Quit", false, true, false }, { "Next", true, true, false } })) {
                case 0: result = 1; break;
                case 1: go(Page::GameFiles); break;
                }
            }
            break;
        case Page::GameFiles:
            switch (nav(y, right, { { "Back", false, true, true }, { "Next", true, base_ready() && !busy, false } })) {
            case 0: go(Page::Welcome); break;
            case 1: go(Page::Dlc); break;
            }
            break;
        case Page::Dlc:
            switch (nav(y, right,
                        { { "Back", false, true, true }, { plan.dlc.empty() ? "Skip" : "Next", true, !busy, false } })) {
            case 0: go(Page::GameFiles); break;
            case 1: go(update_required() ? Page::Mods : need_keys() ? Page::Keys : Page::Confirm); break;
            }
            break;
        case Page::Mods:
            switch (nav(y, right, { { "Back", false, true, true },
                                    { plan.mods.empty() ? "Skip" : "Next", true, !busy && !needs_voice_pack(plan, have),
                                      false } })) {
            case 0: go(Page::Dlc); break;
            case 1: go(need_keys() ? Page::Keys : Page::Confirm); break;
            }
            break;
        case Page::Keys:
            switch (nav(y, right, { { "Back", false, true, true }, { "Next", true, keys_ok(), false } })) {
            case 0: go(update_required() ? Page::Mods : Page::Dlc); break;
            case 1: go(Page::Confirm); break;
            }
            break;
        case Page::Confirm: {
            const Page prev = need_keys() ? Page::Keys : update_required() ? Page::Mods : Page::Dlc;
            if (!anything() && !make_elf_needed()) {
                switch (nav(y, right, { { "Back", false, true, true },
                                        { have.ready() ? "Play" : "Game Files", true, true, false } })) {
                case 0: go(prev); break;
                case 1:
                    if (have.ready())
                        result = 0;
                    else
                        go(Page::GameFiles);
                    break;
                }
            } else {
                const uint64_t need = plan.bytes(), space = free_bytes(base);
                switch (nav(y, right, { { "Back", false, true, true },
                                        { "Install", true, !busy && !(space && need > space), false } })) {
                case 0: go(prev); break;
                case 1: start_install(); break;
                }
            }
            break;
        }
        case Page::Installing:
            if (nav(y, right, { { cancel ? "Cancelling..." : "Cancel", false, !cancel.load(), false } }) == 0)
                cancel = true;
            break;
        case Page::Done:
            switch (nav(y, right, { { "Close", false, true, false }, { "Play", true, true, false } })) {
            case 0: result = 1; break;
            case 1: result = 0; break;
            }
            break;
        case Page::Failed:
            switch (nav(y, right, { { have.ready() ? "Close" : "Quit", false, true, false }, { "Back", true, true, false } })) {
            case 0: result = 1; break;
            case 1:
                cancel = false;
                go(Page::Confirm);
                break;
            }
            break;
        }
        return result;
    }

    /* ---- the frame ---------------------------------------------------- */

    void heading(const char** h, const char** sub) const
    {
        switch (page) {
        case Page::Welcome:
            *h = have.ready() ? "Game Files" : "Welcome";
            *sub = have.ready() ? "Drakengard 3 is installed. Add DLC here, or replace any of the game's files."
                                : "Drakengard 3 Recompiled installs the game from your own copy of it.";
            break;
        case Page::GameFiles:
            *h = "Game Files";
            *sub = update_required() ? "The game and its 1.01 update." : "The game disc.";
            break;
        case Page::Dlc:
            *h = "DLC";
            *sub = "Optional: the packs you own. More can be added later, from the launcher.";
            break;
        case Page::Mods:
            *h = "Mods";
            *sub = "Optional: fan-made additions, installed beside your own game files.";
            break;
        case Page::Keys:
            *h = "Keys";
            *sub = "The keys that open the game's executable, EBOOT.BIN.";
            break;
        case Page::Confirm:
            *h = "Install";
            *sub = anything() || make_elf_needed() ? "Ready to install." : "Nothing new to install.";
            break;
        case Page::Installing:
            *h = "Installing";
            *sub = "The game's files are copied beside the game; this takes a few minutes.";
            break;
        case Page::Done:
            *h = "Installed";
            *sub = "Drakengard 3 is ready to play.";
            break;
        case Page::Failed:
            *h = cancel ? "Cancelled" : "Installation Failed";
            *sub = cancel ? "Nothing more was installed." : "Something went wrong while installing.";
            break;
        }
    }

    int frame()
    {
        const Page before = page;
        begin_frame();
        page_frames++;
        test_grab();
        poll_checks();
        poll_keys();
        poll_art();
        if (page == Page::Installing && finished) {
            worker.join();
            have = installed(base);
            if (ok) {
                plan = Plan();
                go(Page::Done);
            } else
                go(Page::Failed);
        }
        const ImGuiIO& io = ImGui::GetIO();
        backdrop(art, art ? ease_out((float)(now - art_at) / 1.2f) : 0.0f, 1.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##setup", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoScrollWithMouse);
        title();
        steps();

        const char *h = "", *sub = nullptr;
        heading(&h, &sub);
        ImVec2 body, size;
        panel_frame(h, sub, ease_out((float)now / 0.3f), false, &body, &size);
        ImGui::SetCursorScreenPos(body);
        if (page != before || page_new) ImGui::SetNextWindowScroll(ImVec2(0, 0));   /* a page opens at its top */
        ImGui::BeginChild("##page", ImVec2(size.x, size.y - px(64)), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground);
        switch (page) {
        case Page::Welcome: page_welcome(); break;
        case Page::GameFiles: page_game_files(); break;
        case Page::Dlc: page_dlc(); break;
        case Page::Mods: page_mods(); break;
        case Page::Keys: page_keys(); break;
        case Page::Confirm: page_confirm(); break;
        case Page::Installing: page_installing(); break;
        case Page::Done: page_done(); break;
        case Page::Failed: page_failed(); break;
        }
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::EndChild();
        const int result = foot(body, size);
        if (page == before) page_new = false;   /* a new page focuses its primary button next frame */
        ImGui::End();
        toast_draw();
        return result;
    }
};

}  // namespace

int run_installer(const fs::path& base)
{
    if (!ui_init("Drakengard 3 Recompiled Setup", 1280, 720, "", 17.0f)) return -1;
    look::style();
    Wizard w;
    look::load_fonts(w.F);
    w.base = base;
    w.art = look::load_art(base);
    if (const char* a = getenv("DOD3_SETUP_AUTO")) w.auto_left = atoi(a);
    if (const char* g = getenv("DOD3_SETUP_GRAB")) w.grab = g;
    if (const char* g = getenv("DOD3_SETUP_GRAB_AT")) w.grab_at = std::max(21, atoi(g));
    if (const char* f = getenv("DOD3_SETUP_ADD")) {   /* '|'-separated */
        std::vector<fs::path> paths;
        for (const char* s = f; *s;) {
            const char* e = strchr(s, '|');
            const std::string one = e ? std::string(s, e) : std::string(s);
            if (!one.empty()) paths.push_back(fs::u8path(one));
            if (!e) break;
            s = e + 1;
        }
        w.add(paths);
    }
    w.have = installed(base);
    {   /* the keys this install was made with, if any */
        Keys k;
        std::string err;
        std::error_code ec;
        if (fs::exists(keys_file(base), ec) && keys_load(keys_file(base), &k, &err)) w.fill_boxes(k);
    }
    int result = 1;
    while (ui_frame_begin()) {
        const int r = w.frame();
        ui_frame_end();
        if (r >= 0) {
            result = r;
            break;
        }
    }
    /* the window was closed mid-install: stop the install first */
    if (w.worker.joinable()) {
        w.cancel = true;
        w.worker.join();
    }
    for (auto& c : w.checks) c.fut.wait();
    if (w.running_gen >= 0) w.key_check.wait();
    if (w.art_loading) w.art_job.wait();
    ui_shutdown();   /* the backend lets go of the picture's texture */
    delete w.art;
    return result == 0 && installed(base).ready() ? 0 : 1;
}

}  // namespace dod3setup
