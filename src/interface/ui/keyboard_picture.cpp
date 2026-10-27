/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The keyboard picture: see keyboard_picture.h.

#include <openglad/interface/ui/keyboard_picture.h>

#include <openglad/interface/input_mappings.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/web_back_key.h>
#include <openglad/interface/web_control_defaults.h>
#include <openglad/resources/og_file.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace og::ui::keyboard_picture {

namespace {

// --- the glyph atlas's table (generated) ------------------------------------

struct GlyphStyle {
    const char* name;
    float size;
    int first_glyph;
    int glyph_count;
    int first_kern;
    int kern_count;
    int ascent;
    int descent;
};
struct Glyph {
    std::uint32_t codepoint;
    int x, y, w, h;   // in the atlas
    int left, top;    // the bitmap's top-left from the pen on the baseline
    float advance;
};
struct Kern {
    std::uint32_t left, right;
    float adjust;
};

#include "keyboard_glyphs.inc"

const og::io::RgbaImage* atlas()
{
    static const std::optional<og::io::RgbaImage> image = [] {
        std::optional<og::io::RgbaImage> loaded =
            og::io::read_rgba_png(kAtlasPath);
        // A file that is not the table's atlas would put every glyph in the
        // wrong place: draw no text rather than the wrong text.
        if (loaded && (loaded->w != kAtlasW || loaded->h != kAtlasH))
            loaded.reset();
        return loaded;
    }();
    return image ? &*image : nullptr;
}

const GlyphStyle* style_named(std::string_view name)
{
    for (const GlyphStyle& style : kStyles)
        if (name == style.name)
            return &style;
    return nullptr;
}

const GlyphStyle* action_style(float size)
{
    for (const GlyphStyle& style : kStyles)
        if (std::string_view(style.name) == "action" && style.size == size)
            return &style;
    return nullptr;
}

const Glyph* glyph(const GlyphStyle& style, std::uint32_t codepoint)
{
    for (int i = 0; i < style.glyph_count; ++i) {
        const Glyph& g = kGlyphs[style.first_glyph + i];
        if (g.codepoint == codepoint)
            return &g;
    }
    return nullptr;
}

float kerning(const GlyphStyle& style, std::uint32_t a, std::uint32_t b)
{
    for (int i = 0; i < style.kern_count; ++i) {
        const Kern& k = kKerns[style.first_kern + i];
        if (k.left == a && k.right == b)
            return k.adjust;
    }
    return 0.0f;
}

// UTF-8 to code points (the cap names' arrows are the only non-ASCII).
std::vector<std::uint32_t> codepoints(std::string_view text)
{
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        int extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        std::uint32_t cp = extra == 0 ? c
            : extra == 1             ? (c & 0x1Fu)
            : extra == 2             ? (c & 0x0Fu)
                                     : (c & 0x07u);
        ++i;
        for (; extra > 0 && i < text.size(); --extra, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3Fu);
        out.push_back(cp);
    }
    return out;
}

float text_width(const GlyphStyle& style, std::string_view text)
{
    float width = 0.0f;
    std::uint32_t previous = 0;
    for (const std::uint32_t cp : codepoints(text)) {
        if (const Glyph* g = glyph(style, cp))
            width += g->advance;
        if (previous != 0)
            width += kerning(style, previous, cp);
        previous = cp;
    }
    return width;
}

// --- pixels -----------------------------------------------------------------

struct Rgb {
    float r, g, b;
};
Rgb rgb(std::uint32_t hex)
{
    return {static_cast<float>((hex >> 16) & 0xFF),
            static_cast<float>((hex >> 8) & 0xFF),
            static_cast<float>(hex & 0xFF)};
}

class Canvas {
public:
    explicit Canvas(og::io::RgbaImage& image) : image_(image) {}

    void blend(int x, int y, const Rgb& c, float alpha)
    {
        if (x < 0 || y < 0 || x >= image_.w || y >= image_.h || alpha <= 0.0f)
            return;
        alpha = std::min(alpha, 1.0f);
        std::uint8_t* p = &image_.bytes[(static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(image_.w) +
                                         static_cast<std::size_t>(x)) *
                                        4u];
        const float keep = 1.0f - alpha;
        p[0] = static_cast<std::uint8_t>(std::lround(p[0] * keep + c.r * alpha));
        p[1] = static_cast<std::uint8_t>(std::lround(p[1] * keep + c.g * alpha));
        p[2] = static_cast<std::uint8_t>(std::lround(p[2] * keep + c.b * alpha));
        p[3] = 255;
    }

    // An SVG-style rounded rectangle: the fill reaches the edge, the stroke
    // straddles it. Antialiased from the signed distance at each pixel's
    // centre. `fill_at(x)` picks the fill colour per column (a split cap).
    template <typename FillAt>
    void rounded_rect(float x, float y, float w, float h, float radius,
                      FillAt fill_at, const Rgb& stroke, float stroke_alpha,
                      float stroke_width)
    {
        const float cx = x + w / 2.0f;
        const float cy = y + h / 2.0f;
        const float hx = w / 2.0f - radius;
        const float hy = h / 2.0f - radius;
        const float half_stroke = stroke_width / 2.0f;
        const int x0 = static_cast<int>(std::floor(x - stroke_width));
        const int x1 = static_cast<int>(std::ceil(x + w + stroke_width));
        const int y0 = static_cast<int>(std::floor(y - stroke_width));
        const int y1 = static_cast<int>(std::ceil(y + h + stroke_width));
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const float qx = std::fabs(static_cast<float>(px) + 0.5f - cx) - hx;
                const float qy = std::fabs(static_cast<float>(py) + 0.5f - cy) - hy;
                const float ox = std::max(qx, 0.0f);
                const float oy = std::max(qy, 0.0f);
                const float d = std::sqrt(ox * ox + oy * oy) +
                                std::min(std::max(qx, qy), 0.0f) - radius;
                const float fill = std::clamp(0.5f - d, 0.0f, 1.0f);
                if (fill > 0.0f)
                    blend(px, py, fill_at(px), fill);
                const float edge =
                    std::clamp(half_stroke + 0.5f - std::fabs(d), 0.0f, 1.0f);
                if (edge > 0.0f)
                    blend(px, py, stroke, edge * stroke_alpha);
            }
        }
    }

    // Text from the atlas, the pen starting at (x, baseline).
    void text(const og::io::RgbaImage& atlas_image, const GlyphStyle& style,
              float x, int baseline, std::string_view text, const Rgb& colour,
              float opacity)
    {
        float pen = x;
        std::uint32_t previous = 0;
        for (const std::uint32_t cp : codepoints(text)) {
            if (previous != 0)
                pen += kerning(style, previous, cp);
            previous = cp;
            const Glyph* g = glyph(style, cp);
            if (g == nullptr)
                continue;
            const int gx = static_cast<int>(std::floor(pen + 0.5f)) + g->left;
            const int gy = baseline + g->top;
            for (int row = 0; row < g->h; ++row) {
                for (int col = 0; col < g->w; ++col) {
                    const std::size_t at =
                        (static_cast<std::size_t>(g->y + row) *
                             static_cast<std::size_t>(atlas_image.w) +
                         static_cast<std::size_t>(g->x + col)) *
                        4u;
                    const float cover = atlas_image.bytes[at] / 255.0f;
                    blend(gx + col, gy + row, colour, cover * opacity);
                }
            }
            pen += g->advance;
        }
    }

private:
    og::io::RgbaImage& image_;
};

// --- the layout ---------------------------------------------------------------

// The approved picture's grid: fifteen units of main block, a 0.3 unit gap,
// three units of navigation block and arrows; six rows, the function row a
// quarter unit apart from the rest.
constexpr float kMainUnits = 15.0f;
constexpr float kSideGapUnits = 0.3f;
constexpr float kSideX = kMainUnits + kSideGapUnits;
constexpr float kTotalUnits = kSideX + 3.0f;
constexpr float kFRowGapUnits = 0.25f;
constexpr float kRowCount = 6.0f;
constexpr float kRadius = 8.0f;

float row_y(int row)
{
    return static_cast<float>(row) + (row > 0 ? kFRowGapUnits : 0.0f);
}

struct RowKey {
    const char* id;  // nullptr: a blank
    const char* text;
    int keycode;
    float w;
};

std::vector<Cap> build_caps()
{
    using R = RowKey;
    const std::vector<std::vector<RowKey>> rows = {
        {R{"Esc", "Esc", KEYCODE_ESCAPE, 1}, R{nullptr, "", 0, 1},
         R{"F1", "F1", KEYCODE_F1, 1}, R{"F2", "F2", KEYCODE_F2, 1},
         R{"F3", "F3", KEYCODE_F3, 1}, R{"F4", "F4", KEYCODE_F4, 1},
         R{nullptr, "", 0, .5f}, R{"F5", "F5", KEYCODE_F5, 1},
         R{"F6", "F6", KEYCODE_F6, 1}, R{"F7", "F7", KEYCODE_F7, 1},
         R{"F8", "F8", KEYCODE_F8, 1}, R{nullptr, "", 0, .5f},
         R{"F9", "F9", KEYCODE_F9, 1}, R{"F10", "F10", KEYCODE_F10, 1},
         R{"F11", "F11", KEYCODE_F11, 1}, R{"F12", "F12", KEYCODE_F12, 1}},
        {R{"`", "`", KEYCODE_BACKQUOTE, 1}, R{"1", "1", KEYCODE_1, 1},
         R{"2", "2", KEYCODE_2, 1}, R{"3", "3", KEYCODE_3, 1},
         R{"4", "4", KEYCODE_4, 1}, R{"5", "5", KEYCODE_5, 1},
         R{"6", "6", KEYCODE_6, 1}, R{"7", "7", KEYCODE_7, 1},
         R{"8", "8", KEYCODE_8, 1}, R{"9", "9", KEYCODE_9, 1},
         R{"0", "0", KEYCODE_0, 1}, R{"-", "-", KEYCODE_MINUS, 1},
         R{"=", "=", KEYCODE_EQUALS, 1},
         R{"Bksp", "Bksp", KEYCODE_BACKSPACE, 2}},
        {R{"Tab", "Tab", KEYCODE_TAB, 1.5f}, R{"Q", "Q", KEYCODE_q, 1},
         R{"W", "W", KEYCODE_w, 1}, R{"E", "E", KEYCODE_e, 1},
         R{"R", "R", KEYCODE_r, 1}, R{"T", "T", KEYCODE_t, 1},
         R{"Y", "Y", KEYCODE_y, 1}, R{"U", "U", KEYCODE_u, 1},
         R{"I", "I", KEYCODE_i, 1}, R{"O", "O", KEYCODE_o, 1},
         R{"P", "P", KEYCODE_p, 1}, R{"[", "[", KEYCODE_LEFTBRACKET, 1},
         R{"]", "]", KEYCODE_RIGHTBRACKET, 1},
         R{"\\", "\\", KEYCODE_BACKSLASH, 1.5f}},
        {R{"Caps", "Caps", KEYCODE_CAPSLOCK, 1.75f}, R{"A", "A", KEYCODE_a, 1},
         R{"S", "S", KEYCODE_s, 1}, R{"D", "D", KEYCODE_d, 1},
         R{"F", "F", KEYCODE_f, 1}, R{"G", "G", KEYCODE_g, 1},
         R{"H", "H", KEYCODE_h, 1}, R{"J", "J", KEYCODE_j, 1},
         R{"K", "K", KEYCODE_k, 1}, R{"L", "L", KEYCODE_l, 1},
         R{";", ";", KEYCODE_SEMICOLON, 1}, R{"'", "'", KEYCODE_QUOTE, 1},
         R{"Enter", "Enter", KEYCODE_RETURN, 2.25f}},
        {R{"LShift", "Shift", KEYCODE_LSHIFT, 2.25f}, R{"Z", "Z", KEYCODE_z, 1},
         R{"X", "X", KEYCODE_x, 1}, R{"C", "C", KEYCODE_c, 1},
         R{"V", "V", KEYCODE_v, 1}, R{"B", "B", KEYCODE_b, 1},
         R{"N", "N", KEYCODE_n, 1}, R{"M", "M", KEYCODE_m, 1},
         R{",", ",", KEYCODE_COMMA, 1}, R{".", ".", KEYCODE_PERIOD, 1},
         R{"/", "/", KEYCODE_SLASH, 1},
         R{"RShift", "Shift", KEYCODE_RSHIFT, 2.75f}},
        {R{"LCtrl", "Ctrl", KEYCODE_LCTRL, 1.25f},
         R{"LWin", "Win", KEYCODE_LGUI, 1.25f},
         R{"LAlt", "Alt", KEYCODE_LALT, 1.25f},
         R{"Space", "Space", KEYCODE_SPACE, 6.25f},
         R{"RAlt", "Alt", KEYCODE_RALT, 1.25f},
         R{"RWin", "Win", KEYCODE_RGUI, 1.25f},
         R{"Menu", "Menu", KEYCODE_APPLICATION, 1.25f},
         R{"RCtrl", "Ctrl", KEYCODE_RCTRL, 1.25f}},
    };
    std::vector<Cap> out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        float x = 0.0f;
        for (const RowKey& key : rows[r]) {
            if (key.id != nullptr)
                out.push_back(Cap{key.id, key.text, key.keycode, x,
                                  row_y(static_cast<int>(r)), key.w});
            x += key.w;
        }
    }
    // The navigation block over rows 1-2 of the side column (it gives
    // Delete, the web P2 LOOK UP, a cap), the arrows on rows 4-5.
    const RowKey nav[2][3] = {
        {R{"Ins", "Ins", KEYCODE_INSERT, 1}, R{"Home", "Home", KEYCODE_HOME, 1},
         R{"PgUp", "PgUp", KEYCODE_PAGEUP, 1}},
        {R{"Del", "Del", KEYCODE_DELETE, 1}, R{"End", "End", KEYCODE_END, 1},
         R{"PgDn", "PgDn", KEYCODE_PAGEDOWN, 1}},
    };
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c)
            out.push_back(Cap{nav[r][c].id, nav[r][c].text, nav[r][c].keycode,
                              kSideX + static_cast<float>(c), row_y(1 + r), 1});
    out.push_back(Cap{"Up", "↑", KEYCODE_UP, kSideX + 1, row_y(4), 1});
    out.push_back(Cap{"Left", "←", KEYCODE_LEFT, kSideX, row_y(5), 1});
    out.push_back(Cap{"Down", "↓", KEYCODE_DOWN, kSideX + 1, row_y(5), 1});
    out.push_back(Cap{"Right", "→", KEYCODE_RIGHT, kSideX + 2, row_y(5), 1});
    return out;
}

const std::vector<Cap>& cap_table()
{
    static const std::vector<Cap> table = build_caps();
    return table;
}

float origin_x()
{
    return (kWidth - kTotalUnits * kUnit) / 2.0f + kGap / 2.0f;
}
float origin_y()
{
    return (kHeight - (kRowCount + kFRowGapUnits) * kUnit) / 2.0f +
           kGap / 2.0f;
}

constexpr std::uint32_t kPlayerColours[4] = {0xe5484d, 0x3e8ef7, 0x30a46c,
                                             0xf5b400};
constexpr std::uint32_t kPageColour = 0x16171a;
constexpr std::uint32_t kPanelColour = 0x1e1f23;
constexpr std::uint32_t kPanelStroke = 0x33353a;
constexpr std::uint32_t kFreeCapStroke = 0x45474d;
constexpr std::uint32_t kFreeNameColour = 0x9a9da4;
constexpr std::uint32_t kInkColour = 0x111111;

// The movement rows' names: the pause player screen's binding panel words.
constexpr const char* kMovementActions[8] = {"UP",   "UP-R", "RIGHT", "DN-R",
                                             "DOWN", "DN-L", "LEFT",  "UP-L"};

// The approved picture's label rules (make_keyboard_map.py), kept as it
// drew them so the picture keeps its look: the player tag sits top right
// only when it clears the cap's name, estimated at 0.72 em a glyph, and
// the action takes the largest size up to 21 that fits, estimated at 0.62
// em a glyph -- here snapped down to the atlas's half-pixel sizes and
// checked against the label's real width.
bool tag_fits(std::size_t name_glyphs, std::size_t tag_glyphs, float cap_w)
{
    const float name_end = 8.0f + 0.72f * 26.0f * static_cast<float>(name_glyphs);
    const float tag_start =
        cap_w - 7.0f - 0.72f * 16.0f * static_cast<float>(tag_glyphs);
    return name_end + 6.0f <= tag_start;
}

const GlyphStyle* fit_action(std::string_view action, float cap_w)
{
    const float room = cap_w - 10.0f;
    const float estimate =
        std::min(21.0f, room / (0.62f * static_cast<float>(std::max<std::size_t>(
                                            1, codepoints(action).size()))));
    const GlyphStyle* smallest = nullptr;
    for (const GlyphStyle& style : kStyles) {
        if (std::string_view(style.name) != "action")
            continue;
        smallest = &style;
        if (style.size <= estimate + 1e-3f && text_width(style, action) <= room)
            return &style;
    }
    return smallest;
}


}  // namespace

std::span<const Cap> caps()
{
    return cap_table();
}

const Cap* cap_for_keycode(int keycode)
{
    if (keycode == KEYCODE_UNKNOWN)
        return nullptr;
    for (const Cap& cap : cap_table())
        if (cap.keycode == keycode)
            return &cap;
    return nullptr;
}

CapRect cap_rect(const Cap& cap)
{
    return {origin_x() + cap.x_units * kUnit, origin_y() + cap.y_units * kUnit,
            cap.w_units * kUnit - kGap, static_cast<float>(kUnit - kGap)};
}

const Cap* cap_at(int px, int py)
{
    const float x = static_cast<float>(px) + 0.5f;
    const float y = static_cast<float>(py) + 0.5f;
    for (const Cap& cap : cap_table()) {
        const CapRect r = cap_rect(cap);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return &cap;
    }
    return nullptr;
}

std::uint32_t player_colour(int player_number)
{
    if (player_number <= 0)
        return kSharedKeyColour;
    return kPlayerColours[(player_number - 1) % 4];
}

std::string_view slot_action(int slot, bool ability_select)
{
    if (slot >= KEY_UP && slot <= KEY_UP_LEFT)
        return kMovementActions[slot];
    if (slot == KEY_CHEAT)
        return "CHEAT";
    for (const BindingPanelRow& row : binding_panel_action_rows(ability_select))
        if (row.key == slot)
            return row.label;
    return {};
}

float action_label_width(std::string_view text, float size)
{
    const GlyphStyle* style = action_style(size);
    if (style == nullptr || atlas() == nullptr)
        return -1.0f;
    return text_width(*style, text);
}

bool glyph_atlas_loaded()
{
    return atlas() != nullptr;
}

Input factory_input(bool web)
{
    Input input;
    input.web = web;
    input.ability_select = true;
    for (int profile = 0; profile < 4; ++profile) {
        Seat seat;
        seat.player_number = profile + 1;
        for (int slot = 0; slot < NUM_KEYS; ++slot)
            seat.keys[static_cast<std::size_t>(slot)] =
                og::input::browser_safe_default_key(
                    profile, og::input::kWebModeFourIndex, slot,
                    og::input::factory_default_key(
                        profile, og::input::kWebModeFourIndex, slot),
                    web);
        input.seats.push_back(seat);
    }
    return input;
}

Output render(const Input& input)
{
    const std::vector<Cap>& table = cap_table();
    std::vector<std::vector<CapUse>> bound(table.size());
    Output out;
    const auto index_of = [&table](const Cap* cap) {
        return static_cast<std::size_t>(cap - table.data());
    };

    // The key every player shares: Escape, or Backspace on the web build
    // (web_back_key.h), where the browser keeps Escape.
    const Cap* pause = cap_for_keycode(input.web
                                           ? og::input::kBackKeycodeBackspace
                                           : og::input::kBackKeycodeEscape);
    if (pause != nullptr)
        bound[index_of(pause)].push_back({0, kPauseAction});

    for (const Seat& seat : input.seats) {
        if (!seat.on_keyboard)
            continue;
        for (int slot = 0; slot < NUM_KEYS; ++slot) {
            const bool diagonal = slot == KEY_UP_RIGHT ||
                                  slot == KEY_DOWN_RIGHT ||
                                  slot == KEY_DOWN_LEFT || slot == KEY_UP_LEFT;
            if (diagonal && !seat.eight_direction)
                continue;
            const std::string_view action =
                slot_action(slot, input.ability_select);
            const int key = seat.keys[static_cast<std::size_t>(slot)];
            if (action.empty() || key == KEYCODE_UNKNOWN)
                continue;
            const Cap* cap = cap_for_keycode(key);
            if (cap == nullptr) {
                out.off_picture.push_back(
                    {seat.player_number, action.data(), key});
                continue;
            }
            bound[index_of(cap)].push_back({seat.player_number, action.data()});
        }
    }

    out.picture.w = kWidth;
    out.picture.h = kHeight;
    out.picture.bytes.assign(static_cast<std::size_t>(kWidth) * kHeight * 4u,
                             255);
    const Rgb page = rgb(kPageColour);
    for (std::size_t i = 0; i < out.picture.bytes.size(); i += 4) {
        out.picture.bytes[i] = static_cast<std::uint8_t>(page.r);
        out.picture.bytes[i + 1] = static_cast<std::uint8_t>(page.g);
        out.picture.bytes[i + 2] = static_cast<std::uint8_t>(page.b);
    }
    Canvas canvas(out.picture);
    const Rgb panel = rgb(kPanelColour);
    canvas.rounded_rect(
        4.0f, 4.0f, kWidth - 8.0f, kHeight - 8.0f, 18.0f,
        [&panel](int) { return panel; }, rgb(kPanelStroke), 1.0f, 2.0f);

    const og::io::RgbaImage* atlas_image = atlas();
    out.labels = atlas_image != nullptr;
    const GlyphStyle* name_style = style_named("cap_name");
    const GlyphStyle* free_style = style_named("cap_name_free");
    const GlyphStyle* tag_style = style_named("tag");
    const Rgb ink = rgb(kInkColour);

    for (std::size_t i = 0; i < table.size(); ++i) {
        const Cap& cap = table[i];
        const CapRect r = cap_rect(cap);
        DrawnCap drawn{&cap, {}, {}, 0.0f, false};
        std::vector<const char*> actions;
        for (const CapUse& b : bound[i]) {
            if (std::find(drawn.player_numbers.begin(),
                          drawn.player_numbers.end(),
                          b.player_number) == drawn.player_numbers.end())
                drawn.player_numbers.push_back(b.player_number);
            if (std::find_if(actions.begin(), actions.end(),
                             [&b](const char* a) {
                                 return std::string_view(a) == b.action;
                             }) == actions.end())
                actions.push_back(b.action);
        }
        if (drawn.player_numbers.empty()) {
            const Rgb fill = rgb(kFreeCapColour);
            canvas.rounded_rect(
                r.x, r.y, r.w, r.h, kRadius, [&fill](int) { return fill; },
                rgb(kFreeCapStroke), 1.0f, 1.5f);
            if (atlas_image != nullptr && free_style != nullptr)
                canvas.text(*atlas_image, *free_style, r.x + 9.0f,
                            static_cast<int>(std::lround(r.y + 27.0f)),
                            cap.text, rgb(kFreeNameColour), 1.0f);
            out.drawn.push_back(std::move(drawn));
            continue;
        }
        // Two or more seats on one cap: equal stripes in their colours,
        // left to right in the order the seats were given.
        if (drawn.player_numbers.size() > 1) {
            out.shared.push_back(SharedCap{&cap, drawn.player_numbers, bound[i]});
        }
        std::vector<Rgb> fills;
        for (const int player : drawn.player_numbers)
            fills.push_back(rgb(player_colour(player)));
        const float stripe = r.w / static_cast<float>(fills.size());
        canvas.rounded_rect(
            r.x, r.y, r.w, r.h, kRadius,
            [&fills, &r, stripe](int px) {
                const auto at = static_cast<std::size_t>(std::clamp(
                    static_cast<int>((static_cast<float>(px) + 0.5f - r.x) / stripe), 0,
                    static_cast<int>(fills.size()) - 1));
                return fills[at];
            },
            rgb(0xffffff), 0.35f, 1.5f);

        for (std::size_t a = 0; a < actions.size(); ++a) {
            if (a > 0)
                drawn.action += "/";
            drawn.action += actions[a];
        }
        std::string tag;
        for (const int player : drawn.player_numbers) {
            if (!tag.empty())
                tag += "+";
            tag += player == 0 ? std::string("ALL")
                               : "P" + std::to_string(player);
        }
        drawn.tag = tag_fits(codepoints(cap.text).size(), tag.size(), r.w);
        if (atlas_image != nullptr && name_style != nullptr &&
            tag_style != nullptr) {
            canvas.text(*atlas_image, *name_style, r.x + 8.0f,
                        static_cast<int>(std::lround(r.y + 29.0f)), cap.text,
                        ink, 1.0f);
            if (drawn.tag)
                canvas.text(*atlas_image, *tag_style,
                            r.x + r.w - 7.0f - text_width(*tag_style, tag),
                            static_cast<int>(std::lround(r.y + 24.0f)), tag,
                            ink, 0.72f);
            if (const GlyphStyle* action = fit_action(drawn.action, r.w)) {
                drawn.action_size = action->size;
                const float width = text_width(*action, drawn.action);
                canvas.text(*atlas_image, *action,
                            r.x + r.w / 2.0f - width / 2.0f,
                            static_cast<int>(std::lround(r.y + r.h - 11.0f)),
                            drawn.action, ink, 1.0f);
            }
        }
        out.drawn.push_back(std::move(drawn));
    }
    out.picture.serial = og::io::next_rgba_image_serial();
    return out;
}

}  // namespace og::ui::keyboard_picture
