/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// og.family / og.anims / og.pack — the DECLARATION pass.
//
// A family file says what a family IS and what it DOES in one chunk. The
// two halves have different lifetimes, so the chunk runs in two contexts
// (family_decl.h): here, in a throwaway VM, the call is read for its DATA
// and harvested into the interchange structs the installer consumes; in an
// ordinary world VM the same call binds behavior only.
//
// Everything the harvest reads is a plain value. Functions (hooks, specials
// casts) are type-checked and dropped — they belong to the VM that will run
// them, and this VM is about to be destroyed.

#include <openglad/gameplay/script/family_decl.h>

#include <openglad/core/family_presentation.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/gameplay/walker.h>

#include "script_host_impl.h"
#include "script_internal.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace og::script {

namespace {

// --- og.NIL -------------------------------------------------------------
//
// One address, process-wide, pushed as light userdata: identity comparison,
// no allocation, and the same value in every VM (a table would be per-VM and
// a string would collide with pack data).
char g_og_nil_tag = 0;

// --- the deferred family reference --------------------------------------

int family_ref_use_error(lua_State* L)
{
    return luaL_error(
        L, "og.family_id: a family byte cannot be READ while families are "
           "being declared — the ids are assigned by the install this "
           "declaration feeds. The call answers a truthy placeholder so the "
           "`assert(og.family_id(...))` idiom still guards; move any actual "
           "use into a hook, which runs after install");
}

}  // namespace

void push_og_nil(lua_State* L)
{
    lua_pushlightuserdata(L, &g_og_nil_tag);
}

bool is_og_nil(lua_State* L, int idx)
{
    return lua_type(L, idx) == LUA_TLIGHTUSERDATA &&
           lua_touserdata(L, idx) == &g_og_nil_tag;
}

void push_family_ref(lua_State* L)
{
    lua_newuserdatauv(L, 1, 0);
    if (luaL_newmetatable(L, kFamilyRefMeta)) {
        // Every way of USING the value raises; being truthy is the whole of
        // what it can do.
        static const char* const kBlocked[] = {
            "__index",    "__newindex", "__call", "__tostring", "__concat",
            "__len",      "__add",      "__sub",  "__mul",      "__div",
            "__mod",      "__pow",      "__unm",  "__idiv",     "__lt",
            "__le",
        };
        for (const char* event : kBlocked) {
            lua_pushcfunction(L, family_ref_use_error);
            lua_setfield(L, -2, event);
        }
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
    }
    lua_setmetatable(L, -2);
}

namespace {

// --- harvest plumbing ---------------------------------------------------
//
// Errors travel back as a string rather than through lua_error, so the whole
// walk stays ordinary C++ with ordinary destructors and one raise site (the
// og.family entry point). `where` names the exact field a modder has to go
// look at: "og.family living 'core:soldier'.combat.hp".

struct Harvest {
    lua_State* L;
    std::string err;

    bool fail(const std::string& message)
    {
        if (err.empty())
            err = message;
        return false;
    }

    // Every reader takes the value at the top of the stack and POPS it,
    // failure or not, so the caller's stack depth is the same either way.
    bool take_string(const std::string& where, std::string& out);
    bool take_int(const std::string& where, std::int32_t& out);
    bool take_number(const std::string& where, float& out);
    bool take_bool(const std::string& where, bool& out);
    bool take_string_list(const std::string& where,
                          std::vector<std::string>& out);
};

const char* type_name_of(lua_State* L, int idx)
{
    if (is_og_nil(L, idx))
        return "og.NIL";
    return luaL_typename(L, idx);
}

bool Harvest::take_string(const std::string& where, std::string& out)
{
    // Deliberately NOT lua_tostring: that coerces a number, and a pack that
    // wrote `name = 12` meant something else.
    if (lua_type(L, -1) != LUA_TSTRING) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return fail(where + ": expected a string, got " + got);
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    out.assign(s, len);
    lua_pop(L, 1);
    return true;
}

bool Harvest::take_int(const std::string& where, std::int32_t& out)
{
    if (!lua_isinteger(L, -1)) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return fail(where + ": expected a whole number, got " + got);
    }
    const lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (v < -2147483648LL || v > 2147483647LL)
        return fail(where + ": " + std::to_string(v) + " is out of range");
    out = static_cast<std::int32_t>(v);
    return true;
}

bool Harvest::take_number(const std::string& where, float& out)
{
    if (lua_type(L, -1) != LUA_TNUMBER) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return fail(where + ": expected a number, got " + got);
    }
    out = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return true;
}

bool Harvest::take_bool(const std::string& where, bool& out)
{
    // Strict: `leaves_bloodspot = 0` is true in Lua, and a modder who wrote
    // it meant false.
    if (lua_type(L, -1) != LUA_TBOOLEAN) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return fail(where + ": expected true or false, got " + got);
    }
    out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return true;
}

bool Harvest::take_string_list(const std::string& where,
                               std::vector<std::string>& out)
{
    if (lua_type(L, -1) != LUA_TTABLE) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return fail(where + ": expected a list, got " + got);
    }
    const lua_Integer n = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        std::string item;
        if (!take_string(where + "[" + std::to_string(i) + "]", item)) {
            lua_pop(L, 1);
            return false;
        }
        out.push_back(std::move(item));
    }
    lua_pop(L, 1);
    return true;
}

// --- field access -------------------------------------------------------
//
// A declaration is SPARSE: a key the table does not spell is not "absent
// data" but "leave the slot's current value alone" (format spec V6/V7), so
// presence is what every reader keys off.

bool has_field(lua_State* L, int tbl, const char* key)
{
    lua_getfield(L, tbl, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// og.NIL where a plain value belongs. The distinction is real — see V6 — so
// say which fields can carry it rather than quietly treating it as absent.
bool reject_og_nil(Harvest& h, const std::string& where)
{
    if (!is_og_nil(h.L, -1))
        return false;
    lua_pop(h.L, 1);
    h.fail(where + ": og.NIL means \"explicitly none\", which this field has "
                   "no reading for — leave the key out instead");
    return true;
}

template <typename T, typename Reader>
bool opt_field(Harvest& h, int tbl, const std::string& where, const char* key,
               std::optional<T>& out, Reader reader)
{
    if (!has_field(h.L, tbl, key))
        return true;
    if (reject_og_nil(h, where + "." + key))
        return false;
    T value{};
    if (!reader(h, where + "." + key, value))
        return false;
    out = value;
    return true;
}

bool opt_string(Harvest& h, int tbl, const std::string& where,
                const char* key, std::optional<std::string>& out)
{
    return opt_field<std::string>(
        h, tbl, where, key, out,
        [](Harvest& hh, const std::string& w, std::string& v) {
            return hh.take_string(w, v);
        });
}

bool opt_int(Harvest& h, int tbl, const std::string& where, const char* key,
             std::optional<std::int32_t>& out)
{
    return opt_field<std::int32_t>(
        h, tbl, where, key, out,
        [](Harvest& hh, const std::string& w, std::int32_t& v) {
            return hh.take_int(w, v);
        });
}

bool opt_number(Harvest& h, int tbl, const std::string& where,
                const char* key, std::optional<float>& out)
{
    return opt_field<float>(h, tbl, where, key, out,
                            [](Harvest& hh, const std::string& w, float& v) {
                                return hh.take_number(w, v);
                            });
}

bool opt_bool(Harvest& h, int tbl, const std::string& where, const char* key,
              std::optional<bool>& out)
{
    return opt_field<bool>(h, tbl, where, key, out,
                           [](Harvest& hh, const std::string& w, bool& v) {
                               return hh.take_bool(w, v);
                           });
}

bool opt_string_list(Harvest& h, int tbl, const std::string& where,
                     const char* key,
                     std::optional<std::vector<std::string>>& out)
{
    if (!has_field(h.L, tbl, key))
        return true;
    if (reject_og_nil(h, where + "." + key))
        return false;
    std::vector<std::string> list;
    if (!h.take_string_list(where + "." + key, list))
        return false;
    out = std::move(list);
    return true;
}

// The present-null fields: absent / og.NIL / value all stay distinguishable.
bool opt_nullable(Harvest& h, int tbl, const std::string& where,
                  const char* key, og::data::NullableString& out)
{
    if (!has_field(h.L, tbl, key))
        return true;
    out.present = true;
    if (is_og_nil(h.L, -1)) {
        out.is_null = true;
        lua_pop(h.L, 1);
        return true;
    }
    return h.take_string(where + "." + key, out.value);
}

// `wire_id = 0` (the ordinary spelling), `wire_id = "auto"`, or absent. The
// struct carries the text because that is what the installer's auto-id pass
// reads.
bool opt_wire_id(Harvest& h, int tbl, const std::string& where,
                 std::string& out)
{
    if (!has_field(h.L, tbl, "wire_id"))
        return true;
    if (lua_isinteger(h.L, -1)) {
        out = std::to_string(lua_tointeger(h.L, -1));
        lua_pop(h.L, 1);
        return true;
    }
    if (lua_type(h.L, -1) == LUA_TSTRING)
        return h.take_string(where + ".wire_id", out);
    const std::string got = type_name_of(h.L, -1);
    lua_pop(h.L, 1);
    return h.fail(where + ".wire_id: expected a number 0..255 or \"auto\", "
                          "got " + got);
}

// --- strict keys (format spec V5) ---------------------------------------
//
// A key nobody recognizes stops the pack. Skipping it instead is how a typo
// ships as a missing field. `ext` is the one opaque key: a pack straddling
// engine releases parks its future data there and gates on og.api.version.
//
// Refusing a typo is only half a diagnostic; the other half is the name the
// author meant. `hp = 120` misspelled as `hitpoints` is a lookup in the
// docs, `nmae` is a squint at the screen — the suggestion turns both into a
// glance at the error.

}  // namespace

std::string did_you_mean(const std::string& bad,
                         const std::vector<const char*>& known)
{
    // Ordinary Levenshtein over two short identifiers, with the classic
    // one-row rolling buffer: a family declaration has a few dozen keys, so
    // the whole search is well under a microsecond and only runs on the way
    // to a load error anyway.
    const auto distance = [](const std::string& a, const std::string& b) {
        std::vector<size_t> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); j++)
            row[j] = j;
        for (size_t i = 1; i <= a.size(); i++) {
            size_t diagonal = row[0];
            row[0] = i;
            for (size_t j = 1; j <= b.size(); j++) {
                const size_t previous = row[j];
                const size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
                row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                                   diagonal + cost});
                diagonal = previous;
            }
        }
        return row[b.size()];
    };
    const char* best = nullptr;
    size_t best_distance = 0;
    for (const char* candidate : known) {
        const std::string c = candidate;
        const size_t d = distance(bad, c);
        // Half the name may differ, at least one character and at most
        // three: "nmae" finds "name" (a transposition costs two), "hp" does
        // not find "id" (two characters out of two is a different word).
        const size_t budget =
            std::min<size_t>(3, std::max<size_t>(1, c.size() / 2));
        if (d > budget)
            continue;
        if (best == nullptr || d < best_distance ||
            (d == best_distance && std::strcmp(candidate, best) < 0)) {
            best = candidate;
            best_distance = d;
        }
    }
    if (best == nullptr)
        return {};
    return std::string(" (did you mean '") + best + "'?)";
}

namespace {

bool check_keys(Harvest& h, int tbl, const std::string& where,
                const std::vector<const char*>& known)
{
    lua_State* L = h.L;
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        lua_pop(L, 1);  // value; the key stays for the next iteration
        if (lua_type(L, -1) != LUA_TSTRING)
            continue;
        const char* key = lua_tostring(L, -1);
        bool ok = std::strcmp(key, "ext") == 0;
        for (const char* k : known)
            ok = ok || std::strcmp(k, key) == 0;
        if (!ok) {
            const std::string bad = key;
            lua_pop(L, 1);
            return h.fail(where + ": unknown key '" + bad + "'" +
                          did_you_mean(bad, known));
        }
    }
    return true;
}

// --- shared sub-tables --------------------------------------------------

// radar_color takes the two sentinel spellings on top of a palette index, so
// a pack can say "no blip" / "the entity's colour" without knowing the
// numbers.
bool take_radar_color(Harvest& h, const std::string& where,
                      std::optional<std::int32_t>& out)
{
    if (lua_type(h.L, -1) == LUA_TSTRING) {
        std::string text;
        if (!h.take_string(where, text))
            return false;
        if (text == og::kRadarColorNoneName)
            out = og::kRadarColorNone;
        else if (text == og::kRadarColorTeamName)
            out = og::kRadarColorTeam;
        else
            return h.fail(where + ": unknown radar colour '" + text +
                          "' (a palette index, \"" +
                          og::kRadarColorNoneName + "\" or \"" +
                          og::kRadarColorTeamName + "\")");
        return true;
    }
    std::int32_t value = 0;
    if (!h.take_int(where, value))
        return false;
    out = value;
    return true;
}

bool harvest_presentation(Harvest& h, int tbl, const std::string& where,
                          og::data::ClasspackPresentation& p)
{
    if (!opt_string(h, tbl, where, "glyph", p.glyph) ||
        !opt_string(h, tbl, where, "glyph_ascii", p.glyph_ascii) ||
        !opt_string(h, tbl, where, "glyph_color", p.glyph_color) ||
        !opt_bool(h, tbl, where, "glyph_bold", p.glyph_bold) ||
        !opt_bool(h, tbl, where, "glyph_transparent", p.glyph_transparent) ||
        !opt_int(h, tbl, where, "radar_jitter", p.radar_jitter))
        return false;
    if (!has_field(h.L, tbl, "radar_color"))
        return true;
    return take_radar_color(h, where + ".radar_color", p.radar_color);
}

// `tuning = { ... }` — the pack's own numbers, read back by scripts through
// og.tuning(self). Lua's subtype is the author's spelling: `5` is an
// integer, `5.0` a number, and a quoted `"5"` a string.
//
// Pairs land SORTED BY KEY. A Lua table has no source order to preserve and
// the store is keyed by name, so sorting is what makes the harvested vector
// a function of the declaration's content alone — identical on every peer,
// and comparable field-for-field.
bool harvest_tuning(Harvest& h, int tbl, const std::string& where,
                    std::vector<og::data::ClasspackTuningPair>& out)
{
    if (!has_field(h.L, tbl, "tuning"))
        return true;
    lua_State* L = h.L;
    if (lua_type(L, -1) != LUA_TTABLE) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return h.fail(where + ".tuning: expected a table of named values, "
                              "got " + got);
    }
    const int tuning_tbl = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, tuning_tbl) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 3);
            return h.fail(where + ".tuning: keys must be names");
        }
        og::data::ClasspackTuningPair pair;
        pair.key = lua_tostring(L, -2);
        const std::string vwhere = where + ".tuning." + pair.key;
        switch (lua_type(L, -1)) {
            case LUA_TBOOLEAN:
                pair.value.kind = og::data::ClasspackTuningValue::Kind::Boolean;
                pair.value.boolean = lua_toboolean(L, -1) != 0;
                break;
            case LUA_TSTRING:
                pair.value.kind = og::data::ClasspackTuningValue::Kind::String;
                pair.value.string = lua_tostring(L, -1);
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1)) {
                    pair.value.kind =
                        og::data::ClasspackTuningValue::Kind::Integer;
                    pair.value.integer = lua_tointeger(L, -1);
                } else {
                    pair.value.kind =
                        og::data::ClasspackTuningValue::Kind::Number;
                    pair.value.number = lua_tonumber(L, -1);
                }
                break;
            default: {
                const std::string got = type_name_of(L, -1);
                lua_pop(L, 3);
                return h.fail(vwhere + ": a tuning value is a number, a "
                                       "string or a boolean, got " + got);
            }
        }
        out.push_back(std::move(pair));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // the tuning table
    std::sort(out.begin(), out.end(),
              [](const og::data::ClasspackTuningPair& a,
                 const og::data::ClasspackTuningPair& b) {
                  return a.key < b.key;
              });
    return true;
}

// --- specials -----------------------------------------------------------

bool is_special_id(const std::string& id)
{
    if (id.empty())
        return false;
    for (const char ch : id) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                        ch == '_';
        if (!ok)
            return false;
    }
    return true;
}

// What the behavior half of a `specials` list contains. The harvest drops
// the functions themselves — they belong to the bind pass — but the family's
// other keys have to agree with them, so it reports what it saw.
struct SpecialsBehavior {
    bool any_cast = false;  // an entry's `cast`, including `cast = false`
    bool any_ai = false;    // an entry's `ai` sugar
};

// One `specials` entry. `cast` / `ai` are behavior: type-checked here (a
// misspelled function name is nil, and finding that out at bind time in a
// different VM helps nobody) and left for the bind pass to install.
bool harvest_special(Harvest& h, int tbl, const std::string& where,
                     og::data::ClasspackSpecialEntry& out,
                     SpecialsBehavior& behavior)
{
    lua_State* L = h.L;
    if (!has_field(L, tbl, "id"))
        return h.fail(where + ": a special needs an id");
    if (!h.take_string(where + ".id", out.id))
        return false;
    if (!is_special_id(out.id))
        return h.fail(where + ".id '" + out.id +
                      "': a special id is lowercase letters, digits and "
                      "underscores");
    if (out.id == "default")
        return h.fail(where + ".id: 'default' is reserved — it is the "
                              "specials table's catch-all key, so a special "
                              "could never be handled by name");
    const std::string named = where + " '" + out.id + "'";
    if (!has_field(L, tbl, "name"))
        return h.fail(named + ": a special needs a name (the HUD string)");
    if (!h.take_string(named + ".name", out.name))
        return false;
    if (!has_field(L, tbl, "mp_cost"))
        return h.fail(named + ": a special needs an mp_cost");
    if (!h.take_int(named + ".mp_cost", out.mp_cost))
        return false;
    // A cost at or above the sentinel is unspellable (format spec V2): that
    // is the registry's own "this slot holds no special" marker, and a
    // declaration says the same thing by not listing the special at all.
    if (out.mp_cost >= static_cast<std::int32_t>(kSpecialCostDisabled))
        return h.fail(named + ".mp_cost " + std::to_string(out.mp_cost) +
                      ": " + std::to_string(kSpecialCostDisabled) +
                      " and up is the registry's own \"disabled\" marker — "
                      "leave the special out of the list instead, which is "
                      "what absence means");
    if (out.mp_cost < 0)
        return h.fail(named + ".mp_cost " + std::to_string(out.mp_cost) +
                      ": a special cannot cost negative magic");
    if (has_field(L, tbl, "slot") && !h.take_int(named + ".slot", out.slot))
        return false;
    if (has_field(L, tbl, "alternate")) {
        if (lua_type(L, -1) != LUA_TTABLE) {
            const std::string got = type_name_of(L, -1);
            lua_pop(L, 1);
            return h.fail(named + ".alternate takes a table: "
                                  "alternate = { name = \"...\" }, got " +
                          got);
        }
        const int alt = lua_gettop(L);
        std::optional<std::string> alt_name;
        const bool ok = opt_string(h, alt, named + ".alternate", "name",
                                   alt_name);
        lua_pop(L, 1);
        if (!ok)
            return false;
        if (!alt_name.has_value())
            return h.fail(named + ".alternate: needs a name");
        out.alternate_name = std::move(alt_name);
    }
    for (const char* key : {"cast", "ai"}) {
        if (!has_field(L, tbl, key))
            continue;
        if (std::strcmp(key, "ai") == 0)
            behavior.any_ai = true;
        else
            behavior.any_cast = true;
        // `cast = false` is the explicit charged-no-op spelling (format spec
        // V2): the slot casts, spends its MP and does nothing, said out loud.
        const bool ok = lua_isfunction(L, -1) ||
                        (std::strcmp(key, "cast") == 0 &&
                         lua_type(L, -1) == LUA_TBOOLEAN &&
                         lua_toboolean(L, -1) == 0);
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        if (!ok)
            return h.fail(named + "." + key + ": expected a function, got " +
                          got);
    }
    return check_keys(h, tbl, named,
                      {"id", "name", "mp_cost", "slot", "cast", "ai",
                       "alternate"});
}

// `specials` — an ARRAY, position giving slots 1..5, with `slot = N` to
// leave a hole. The slot a cast lands in is decided here so the installed
// descriptor and the bind-pass id join agree by construction.
bool harvest_specials(Harvest& h, int tbl, const std::string& where,
                      std::optional<std::vector<og::data::ClasspackSpecialEntry>>& out,
                      SpecialsBehavior& behavior)
{
    if (!has_field(h.L, tbl, "specials"))
        return true;
    lua_State* L = h.L;
    if (lua_type(L, -1) != LUA_TTABLE) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return h.fail(where + ".specials: expected a list, got " + got);
    }
    const int list = lua_gettop(L);
    std::vector<og::data::ClasspackSpecialEntry> entries;
    const lua_Integer n = luaL_len(L, list);
    int previous_slot = 0;
    bool ok = true;
    for (lua_Integer i = 1; ok && i <= n; i++) {
        const std::string ewhere =
            where + ".specials[" + std::to_string(i) + "]";
        lua_rawgeti(L, list, i);
        if (lua_type(L, -1) != LUA_TTABLE) {
            const std::string got = type_name_of(L, -1);
            lua_pop(L, 1);
            ok = h.fail(ewhere + ": expected a table, got " + got);
            break;
        }
        og::data::ClasspackSpecialEntry entry;
        ok = harvest_special(h, lua_gettop(L), ewhere, entry, behavior);
        lua_pop(L, 1);
        if (!ok)
            break;
        for (const og::data::ClasspackSpecialEntry& seen : entries) {
            if (seen.id == entry.id)
                ok = h.fail(ewhere + ".id '" + entry.id +
                            "': already used by an earlier special of this "
                            "family");
        }
        if (!ok)
            break;
        if (entry.slot == 0)
            entry.slot = previous_slot + 1;
        if (entry.slot < 1 || entry.slot > og::data::kMaxSpecialSlot) {
            ok = h.fail(ewhere + ": slot " + std::to_string(entry.slot) +
                        " does not exist (a family has slots 1.." +
                        std::to_string(og::data::kMaxSpecialSlot) + ")");
            break;
        }
        if (entry.slot <= previous_slot) {
            ok = h.fail(ewhere + ": slot " + std::to_string(entry.slot) +
                        " must be greater than the previous entry's slot " +
                        std::to_string(previous_slot) +
                        " (specials are listed in slot order)");
            break;
        }
        previous_slot = entry.slot;
        entries.push_back(std::move(entry));
    }
    // `default_cast` is the any-slot handler; behavior, checked and dropped.
    if (ok && has_field(L, list, "default_cast")) {
        behavior.any_cast = true;
        const bool is_fn = lua_isfunction(L, -1);
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        if (!is_fn)
            ok = h.fail(where + ".specials.default_cast: expected a "
                                "function, got " + got);
    }
    // The list is an array with exactly one named key. Checking it catches
    // `default_case` — a typo that would otherwise leave every slot
    // unhandled and only surface as the V3 error at the end of the load.
    if (ok)
        ok = check_keys(h, list, where + ".specials", {"default_cast"});
    lua_pop(L, 1);  // the specials list
    if (!ok)
        return false;
    out = std::move(entries);
    return true;
}

// The hook names this order accepts inline (`on_death = fn` beside
// `is_undead = true`). One vocabulary, shared with og.register_hooks, so a
// name that works in one works in the other.
void append_hook_keys(const OrderInfo* oi, std::vector<const char*>& keys)
{
    for (size_t i = 0; i < oi->hook_count; i++)
        keys.push_back(oi->hooks[i].name);
}

// Every declared hook is type-checked and dropped: it belongs to the VM that
// will run it, and this one is about to be destroyed.
bool check_hook_types(Harvest& h, int tbl, const std::string& where,
                      const OrderInfo* oi)
{
    lua_State* L = h.L;
    for (size_t i = 0; i < oi->hook_count; i++) {
        const char* name = oi->hooks[i].name;
        if (!has_field(L, tbl, name))
            continue;
        const bool is_fn = lua_isfunction(L, -1);
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        if (!is_fn)
            return h.fail(where + "." + name + ": a hook is a function, got " +
                          got);
    }
    return true;
}

// --- the living blocks --------------------------------------------------
//
// Every member of stats: and combat: is required when the block appears:
// defaulting one silently would ship, say, a 0-armor class.

bool req_int(Harvest& h, int tbl, const std::string& where, const char* key,
             std::int32_t& out)
{
    if (!has_field(h.L, tbl, key))
        return h.fail(where + ": missing '" + key + "'");
    return h.take_int(where + "." + key, out);
}

bool req_number(Harvest& h, int tbl, const std::string& where,
                const char* key, float& out)
{
    if (!has_field(h.L, tbl, key))
        return h.fail(where + ": missing '" + key + "'");
    return h.take_number(where + "." + key, out);
}

// Pushes tbl[key] as a table for a sub-block reader, or answers "absent".
// The block stays on the stack; the caller pops it.
enum class BlockState { Absent, Present, Bad };

BlockState open_block(Harvest& h, int tbl, const std::string& where,
                      const char* key)
{
    if (!has_field(h.L, tbl, key))
        return BlockState::Absent;
    if (lua_type(h.L, -1) == LUA_TTABLE)
        return BlockState::Present;
    const std::string got = type_name_of(h.L, -1);
    lua_pop(h.L, 1);
    h.fail(where + "." + key + ": expected a table, got " + got);
    return BlockState::Bad;
}

bool harvest_stats(Harvest& h, int tbl, const std::string& where,
                   std::optional<og::data::ClasspackStatsBlock>& out)
{
    const BlockState state = open_block(h, tbl, where, "stats");
    if (state != BlockState::Present)
        return state == BlockState::Absent;
    const int b = lua_gettop(h.L);
    const std::string w = where + ".stats";
    og::data::ClasspackStatsBlock stats;
    // Strict keys FIRST, before the required-member walk. A misspelled axis
    // is both an unknown key and a missing one, and only the first reading
    // tells the author what to type: "unknown key 'strenght' (did you mean
    // 'strength'?)" beats "missing 'strength'" over a line that plainly has
    // a strength on it.
    const bool ok =
        check_keys(h, b, w,
                   {"strength", "dexterity", "constitution", "intelligence",
                    "armor", "level"}) &&
        req_int(h, b, w, "strength", stats.strength) &&
        req_int(h, b, w, "dexterity", stats.dexterity) &&
        req_int(h, b, w, "constitution", stats.constitution) &&
        req_int(h, b, w, "intelligence", stats.intelligence) &&
        req_int(h, b, w, "armor", stats.armor) &&
        req_int(h, b, w, "level", stats.level);
    lua_pop(h.L, 1);
    if (!ok)
        return false;
    out = stats;
    return true;
}

bool harvest_combat(Harvest& h, int tbl, const std::string& where,
                    std::optional<og::data::ClasspackCombatBlock>& out)
{
    const BlockState state = open_block(h, tbl, where, "combat");
    if (state != BlockState::Present)
        return state == BlockState::Absent;
    const int b = lua_gettop(h.L);
    const std::string w = where + ".combat";
    og::data::ClasspackCombatBlock combat;
    const bool ok =
        check_keys(h, b, w,
                   {"hp", "melee_damage", "stepsize", "fire_delay",
                    "fire_mp_cost"}) &&
        req_number(h, b, w, "hp", combat.hp) &&
        req_number(h, b, w, "melee_damage", combat.melee_damage) &&
        req_number(h, b, w, "stepsize", combat.stepsize) &&
        req_number(h, b, w, "fire_delay", combat.fire_delay) &&
        req_int(h, b, w, "fire_mp_cost", combat.fire_mp_cost);
    lua_pop(h.L, 1);
    if (!ok)
        return false;
    out = combat;
    return true;
}

// `costs.train` axes are optional — an axis a pack does not price is 0.
// `costs.hire` is not optional.
bool harvest_costs(Harvest& h, int tbl, const std::string& where,
                   std::optional<og::data::ClasspackCostsBlock>& out)
{
    const BlockState state = open_block(h, tbl, where, "costs");
    if (state != BlockState::Present)
        return state == BlockState::Absent;
    const int b = lua_gettop(h.L);
    const std::string w = where + ".costs";
    og::data::ClasspackCostsBlock costs;
    bool ok = check_keys(h, b, w, {"hire", "train"}) &&
              req_int(h, b, w, "hire", costs.hire);
    if (ok) {
        const BlockState train = open_block(h, b, w, "train");
        ok = train != BlockState::Bad;
        if (train == BlockState::Present) {
            const int t = lua_gettop(h.L);
            const std::string tw = w + ".train";
            og::data::ClasspackTrainCosts axes;
            std::optional<std::int32_t> value;
            const std::pair<const char*, std::int32_t*> members[] = {
                {"strength", &axes.strength},
                {"dexterity", &axes.dexterity},
                {"constitution", &axes.constitution},
                {"intelligence", &axes.intelligence},
                {"armor", &axes.armor},
                {"level", &axes.level},
            };
            std::vector<const char*> names;
            for (const auto& m : members)
                names.push_back(m.first);
            ok = check_keys(h, t, tw, names);
            for (const auto& m : members) {
                if (!ok)
                    break;
                value.reset();
                if (!opt_int(h, t, tw, m.first, value)) {
                    ok = false;
                    break;
                }
                if (value.has_value())
                    *m.second = *value;
            }
            lua_pop(h.L, 1);
            if (ok)
                costs.train = axes;
        }
    }
    lua_pop(h.L, 1);
    if (!ok)
        return false;
    out = costs;
    return true;
}

// --- the five orders ----------------------------------------------------
//
// Field for field, one reader per order, filling the interchange structs
// install_pack_families() consumes. The key names are the descriptor field
// names — the mapping is meant to be boring, so that reading a declaration
// tells you what installs.

// The keys every order shares: identity, art, presentation, tuning.
void append_common_keys(std::vector<const char*>& keys)
{
    for (const char* k :
         {"id", "wire_id", "name", "sprite", "animation", "tuning", "glyph",
          "glyph_ascii", "glyph_color", "glyph_bold", "glyph_transparent",
          "radar_color", "radar_jitter"})
        keys.push_back(k);
}

bool harvest_living(Harvest& h, int tbl, const std::string& where,
                    const OrderInfo* oi, og::data::ClasspackLivingEntry& e)
{
    // Per-entry `ai =` is sugar for one family-level check_special_ai, and
    // per-entry `cast =` is the slot form of do_special (see the bind pass).
    // Writing both spellings of either is not a merge, it is two answers to
    // one question, so it stops the pack rather than picking a winner.
    SpecialsBehavior behavior;
    if (!opt_wire_id(h, tbl, where, e.wire_id) ||
        !opt_string(h, tbl, where, "name", e.name) ||
        !opt_nullable(h, tbl, where, "short_name", e.short_name) ||
        !harvest_stats(h, tbl, where, e.stats) ||
        !harvest_combat(h, tbl, where, e.combat) ||
        !harvest_costs(h, tbl, where, e.costs) ||
        !harvest_specials(h, tbl, where, e.specials, behavior) ||
        !opt_string(h, tbl, where, "default_weapon", e.default_weapon) ||
        !opt_string_list(h, tbl, where, "flags", e.init_bit_flags) ||
        !opt_int(h, tbl, where, "init_ani_type", e.init_ani_type) ||
        !opt_number(h, tbl, where, "init_max_magicpoints",
                    e.init_max_magicpoints) ||
        !opt_bool(h, tbl, where, "leaves_bloodspot", e.leaves_bloodspot) ||
        !opt_number(h, tbl, where, "magic_damage_modifier",
                    e.magic_damage_modifier) ||
        !opt_bool(h, tbl, where, "is_stationary", e.is_stationary) ||
        !opt_bool(h, tbl, where, "has_returning_weapon",
                  e.has_returning_weapon) ||
        !opt_bool(h, tbl, where, "is_undead", e.is_undead) ||
        !opt_nullable(h, tbl, where, "promotes_to", e.promotes_to) ||
        !opt_int(h, tbl, where, "promotion_level_req", e.promotion_level_req) ||
        !opt_nullable(h, tbl, where, "death_message", e.death_message) ||
        !opt_nullable(h, tbl, where, "sprite", e.sprite) ||
        !opt_string(h, tbl, where, "animation", e.animation) ||
        !opt_int(h, tbl, where, "ai_line_of_sight", e.ai_line_of_sight) ||
        !opt_nullable(h, tbl, where, "description", e.description) ||
        !opt_string_list(h, tbl, where, "names", e.names) ||
        !opt_bool(h, tbl, where, "playable", e.playable) ||
        !opt_int(h, tbl, where, "playable_order", e.playable_order) ||
        !harvest_presentation(h, tbl, where, e.presentation) ||
        !harvest_tuning(h, tbl, where, e.tuning) ||
        !check_hook_types(h, tbl, where, oi))
        return false;
    if (behavior.any_ai && has_field(h.L, tbl, "check_special_ai")) {
        lua_pop(h.L, 1);
        return h.fail(where + ": a special's `ai =` and a family-level "
                              "check_special_ai are two answers to the same "
                              "question — the per-entry form IS the "
                              "family-level hook, written per slot, so keep "
                              "one of them");
    }
    if (behavior.any_cast && has_field(h.L, tbl, "do_special")) {
        lua_pop(h.L, 1);
        return h.fail(where + ": a special's `cast =` (or a default_cast) and "
                              "a family-level do_special both answer the cast "
                              "— write the slots or the one function, not "
                              "both");
    }
    std::vector<const char*> keys;
    append_common_keys(keys);
    for (const char* k :
         {"short_name", "stats", "combat", "costs", "specials",
          "default_weapon", "flags", "init_ani_type", "init_max_magicpoints",
          "leaves_bloodspot", "magic_damage_modifier", "is_stationary",
          "has_returning_weapon", "is_undead", "promotes_to",
          "promotion_level_req", "death_message", "ai_line_of_sight",
          "description", "names", "playable", "playable_order"})
        keys.push_back(k);
    append_hook_keys(oi, keys);
    return check_keys(h, tbl, where, keys);
}

bool harvest_weapon(Harvest& h, int tbl, const std::string& where,
                    const OrderInfo* oi, og::data::ClasspackWeaponEntry& e)
{
    if (!opt_wire_id(h, tbl, where, e.wire_id) ||
        !opt_string(h, tbl, where, "name", e.name) ||
        !opt_int(h, tbl, where, "fire_sound", e.fire_sound) ||
        !opt_bool(h, tbl, where, "skip_sit_notify", e.skip_sit_notify) ||
        !opt_bool(h, tbl, where, "is_auto_attackable", e.is_auto_attackable) ||
        !opt_string_list(h, tbl, where, "flags", e.init_bit_flags) ||
        !opt_int(h, tbl, where, "init_lifetime", e.init_lifetime) ||
        !opt_int(h, tbl, where, "init_ani_type", e.init_ani_type) ||
        !opt_number(h, tbl, where, "vz", e.vz) ||
        !opt_number(h, tbl, where, "gravity", e.gravity) ||
        !opt_int(h, tbl, where, "sizez", e.sizez) ||
        !opt_bool(h, tbl, where, "can_drop_floors", e.can_drop_floors) ||
        !opt_number(h, tbl, where, "hp", e.hp) ||
        !opt_nullable(h, tbl, where, "sprite", e.sprite) ||
        !opt_string(h, tbl, where, "animation", e.animation) ||
        !harvest_presentation(h, tbl, where, e.presentation) ||
        !harvest_tuning(h, tbl, where, e.tuning) ||
        !check_hook_types(h, tbl, where, oi))
        return false;
    std::vector<const char*> keys;
    append_common_keys(keys);
    for (const char* k :
         {"fire_sound", "skip_sit_notify", "is_auto_attackable", "flags",
          "init_lifetime", "init_ani_type", "vz", "gravity", "sizez",
          "can_drop_floors", "hp"})
        keys.push_back(k);
    append_hook_keys(oi, keys);
    return check_keys(h, tbl, where, keys);
}

bool harvest_effect(Harvest& h, int tbl, const std::string& where,
                    const OrderInfo* oi, og::data::ClasspackEffectEntry& e)
{
    if (!opt_wire_id(h, tbl, where, e.wire_id) ||
        !opt_string(h, tbl, where, "name", e.name) ||
        !opt_bool(h, tbl, where, "loops_animation", e.loops_animation) ||
        !opt_bool(h, tbl, where, "creates_hit_effect", e.creates_hit_effect) ||
        !opt_string_list(h, tbl, where, "flags", e.init_bit_flags) ||
        !opt_number(h, tbl, where, "hp", e.hp) ||
        !opt_nullable(h, tbl, where, "sprite", e.sprite) ||
        !opt_string(h, tbl, where, "animation", e.animation) ||
        !harvest_presentation(h, tbl, where, e.presentation) ||
        !opt_bool(h, tbl, where, "radar_landmark",
                  e.presentation.radar_landmark) ||
        !harvest_tuning(h, tbl, where, e.tuning) ||
        !check_hook_types(h, tbl, where, oi))
        return false;
    std::vector<const char*> keys;
    append_common_keys(keys);
    for (const char* k : {"loops_animation", "creates_hit_effect", "flags",
                          "radar_landmark", "hp"})
        keys.push_back(k);
    append_hook_keys(oi, keys);
    return check_keys(h, tbl, where, keys);
}

bool harvest_treasure(Harvest& h, int tbl, const std::string& where,
                      const OrderInfo* oi, og::data::ClasspackTreasureEntry& e)
{
    if (!opt_wire_id(h, tbl, where, e.wire_id) ||
        !opt_string(h, tbl, where, "name", e.name) ||
        !opt_bool(h, tbl, where, "init_ignore", e.init_ignore) ||
        !opt_int(h, tbl, where, "init_frame", e.init_frame) ||
        !opt_number(h, tbl, where, "hp", e.hp) ||
        !opt_nullable(h, tbl, where, "sprite", e.sprite) ||
        !opt_string(h, tbl, where, "animation", e.animation) ||
        !harvest_presentation(h, tbl, where, e.presentation) ||
        !opt_bool(h, tbl, where, "radar_landmark",
                  e.presentation.radar_landmark) ||
        !harvest_tuning(h, tbl, where, e.tuning) ||
        !check_hook_types(h, tbl, where, oi))
        return false;
    std::vector<const char*> keys;
    append_common_keys(keys);
    for (const char* k : {"init_ignore", "init_frame", "radar_landmark", "hp"})
        keys.push_back(k);
    append_hook_keys(oi, keys);
    return check_keys(h, tbl, where, keys);
}

bool harvest_generator(Harvest& h, int tbl, const std::string& where,
                       const OrderInfo* oi,
                       og::data::ClasspackGeneratorEntry& e)
{
    if (!opt_wire_id(h, tbl, where, e.wire_id) ||
        !opt_string(h, tbl, where, "name", e.name) ||
        !opt_string(h, tbl, where, "default_weapon", e.default_weapon) ||
        !opt_bool(h, tbl, where, "has_lifetime", e.has_lifetime) ||
        !opt_int(h, tbl, where, "spawn_ani_type", e.spawn_ani_type) ||
        !opt_bool(h, tbl, where, "clear_owner", e.clear_owner) ||
        !opt_number(h, tbl, where, "hp", e.hp) ||
        !opt_nullable(h, tbl, where, "sprite", e.sprite) ||
        !opt_string(h, tbl, where, "animation", e.animation) ||
        !opt_string(h, tbl, where, "editor_label", e.editor_label) ||
        !harvest_presentation(h, tbl, where, e.presentation) ||
        !harvest_tuning(h, tbl, where, e.tuning) ||
        !check_hook_types(h, tbl, where, oi))
        return false;
    std::vector<const char*> keys;
    append_common_keys(keys);
    for (const char* k : {"default_weapon", "has_lifetime", "spawn_ani_type",
                          "clear_owner", "editor_label", "hp"})
        keys.push_back(k);
    append_hook_keys(oi, keys);
    return check_keys(h, tbl, where, keys);
}

// The five orders behind one call. `id` is read first because every
// diagnostic below names it: "og.family living 'core:soldier'.combat.hp"
// says which of a merged file's declarations went wrong.
bool harvest_entry(Harvest& h, const OrderInfo* oi, int tbl,
                   og::data::ClasspackData& out)
{
    const std::string order_where = std::string("og.family ") + oi->name;
    if (!has_field(h.L, tbl, "id"))
        return h.fail(order_where + ": a declaration needs an id "
                                    "(id = \"<pack>:<family>\")");
    std::string id;
    if (!h.take_string(order_where + ".id", id))
        return false;
    if (id.find(':') == std::string::npos)
        return h.fail(order_where + ".id '" + id +
                      "': a family id is <pack>:<family>, e.g. "
                      "\"core:soldier\"");
    const std::string where = order_where + " '" + id + "'";
    switch (oi->order) {
        case Order::Living: {
            og::data::ClasspackLivingEntry e;
            e.id = id;
            if (!harvest_living(h, tbl, where, oi, e))
                return false;
            out.living.push_back(std::move(e));
            break;
        }
        case Order::Weapon: {
            og::data::ClasspackWeaponEntry e;
            e.id = id;
            if (!harvest_weapon(h, tbl, where, oi, e))
                return false;
            out.weapons.push_back(std::move(e));
            break;
        }
        case Order::FX: {
            og::data::ClasspackEffectEntry e;
            e.id = id;
            if (!harvest_effect(h, tbl, where, oi, e))
                return false;
            out.effects.push_back(std::move(e));
            break;
        }
        case Order::Treasure: {
            og::data::ClasspackTreasureEntry e;
            e.id = id;
            if (!harvest_treasure(h, tbl, where, oi, e))
                return false;
            out.treasures.push_back(std::move(e));
            break;
        }
        case Order::Generator: {
            og::data::ClasspackGeneratorEntry e;
            e.id = id;
            if (!harvest_generator(h, tbl, where, oi, e))
                return false;
            out.generators.push_back(std::move(e));
            break;
        }
        default:
            return h.fail(order_where + ": this order has no families to "
                                        "declare");
    }
    // Provenance only a declaration can claim: the rules that hold a
    // declared slot to a stricter standard than one a ClasspackData filled
    // any other way read this back after install (format spec V3).
    out.lua_declarations.push_back({oi->order, id});
    return true;
}

// og.anims("name", { rows = N, frames = { {0,1,2,1}, false, ... } }).
// `false` is a NULL row — the legacy anislime table has eight of them, and
// nil cannot say it (a nil in a Lua array is a hole, not a value).
bool harvest_anim_set(Harvest& h, const std::string& name, int tbl,
                      og::data::ClasspackAnimSet& out)
{
    lua_State* L = h.L;
    const std::string where = "og.anims '" + name + "'";
    out.name = name;
    if (!opt_int(h, tbl, where, "rows", out.rows) ||
        !check_keys(h, tbl, where, {"rows", "frames"}))
        return false;
    if (!has_field(L, tbl, "frames"))
        return h.fail(where + ": needs a frames list");
    if (lua_type(L, -1) != LUA_TTABLE) {
        const std::string got = type_name_of(L, -1);
        lua_pop(L, 1);
        return h.fail(where + ".frames: expected a list of rows, got " + got);
    }
    const int rows = lua_gettop(L);
    const lua_Integer n = luaL_len(L, rows);
    bool ok = true;
    for (lua_Integer i = 1; ok && i <= n; i++) {
        const std::string rwhere =
            where + ".frames[" + std::to_string(i) + "]";
        lua_rawgeti(L, rows, i);
        og::data::ClasspackAnimRow row;
        if (lua_type(L, -1) == LUA_TBOOLEAN && lua_toboolean(L, -1) == 0) {
            row.is_null = true;
            lua_pop(L, 1);
        } else if (lua_type(L, -1) != LUA_TTABLE) {
            const std::string got = type_name_of(L, -1);
            lua_pop(L, 1);
            ok = h.fail(rwhere + ": a frame row is a list of frame indices, "
                                 "or false for a null row, got " + got);
            break;
        } else {
            const int row_tbl = lua_gettop(L);
            const lua_Integer len = luaL_len(L, row_tbl);
            for (lua_Integer f = 1; ok && f <= len; f++) {
                lua_rawgeti(L, row_tbl, f);
                std::int32_t frame = 0;
                ok = h.take_int(rwhere + "[" + std::to_string(f) + "]", frame);
                if (ok)
                    row.frames.push_back(frame);
            }
            lua_pop(L, 1);
            if (!ok)
                break;
        }
        out.frames.push_back(std::move(row));
    }
    lua_pop(L, 1);  // the frames list
    return ok;
}

// The og.pack header: the pack's own id, version, title and authors. Last
// one wins across a pack's chunks, so the convention is to put it in the
// file that sorts first (packs/core/families/00-pack.lua).
bool harvest_pack_header(Harvest& h, int tbl, og::data::ClasspackData& out)
{
    const std::string where = "og.pack";
    std::optional<std::string> id;
    std::optional<std::string> version;
    std::optional<std::string> title;
    std::optional<std::vector<std::string>> authors;
    if (!opt_string(h, tbl, where, "id", id) ||
        !opt_string(h, tbl, where, "version", version) ||
        !opt_string(h, tbl, where, "title", title) ||
        !opt_string_list(h, tbl, where, "authors", authors) ||
        !check_keys(h, tbl, where, {"id", "version", "title", "authors"}))
        return false;
    if (id.has_value())
        out.pack = *id;
    if (version.has_value())
        out.version = *version;
    if (title.has_value())
        out.title = *title;
    if (authors.has_value()) {
        // One string, comma-separated: the interchange carries the authors
        // as free text, and the installer hands it straight to the manifest.
        std::string joined;
        for (const std::string& a : *authors) {
            if (!joined.empty())
                joined += ", ";
            joined += a;
        }
        out.authors = joined;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The BIND pass
// ---------------------------------------------------------------------------
//
// The other half of the two-context design: an ordinary VM replays the same
// family chunk, and og.family installs BEHAVIOR only. The data half already
// happened — once, at mount, from the declaration pass — so nothing here
// touches a registry. What it reads is the hook functions and the specials
// casts, and it hangs them in this VM's hook tables exactly where
// og.register_hooks would have put them, so dispatch cannot tell which
// spelling a pack used.
//
// The join is by declared ID, never by list position: a pack that reorders
// or re-costs its specials keeps every cast pointing at the special it was
// written for.

// Marks inside a family's hook table naming the hooks an og.family
// declaration bound. og.register_hooks is the behavior-only OVERRIDE seam
// (format spec V4/V7): layering a hook over a declaration is the point of
// it, not the accident the duplicate warning exists to catch. A string key
// cannot collide with the integer FamilyHook keys dispatch reads.
constexpr const char* kDeclaredHooksKey = "og.declared";

// Pushes the family table's declared-hook set, creating it if needed.
// Expects the family hook table at `family_idx`.
void push_declared_set(lua_State* L, int family_idx)
{
    lua_getfield(L, family_idx, kDeclaredHooksKey);
    if (lua_istable(L, -1))
        return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, family_idx, kDeclaredHooksKey);
}

void note_declared_hook(lua_State* L, int family_idx, FamilyHook hook)
{
    push_declared_set(L, family_idx);
    lua_pushboolean(L, 1);
    lua_rawseti(L, -2, static_cast<lua_Integer>(hook));
    lua_pop(L, 1);
}

}  // namespace

bool take_declared_hook(lua_State* L, int family_idx, FamilyHook hook)
{
    lua_getfield(L, family_idx, kDeclaredHooksKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_rawgeti(L, -1, static_cast<lua_Integer>(hook));
    const bool declared = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    // Taken, not read: the mark answers for ONE override. A second
    // registration of the same hook is two scripts racing, which is exactly
    // what the duplicate report is for.
    lua_pushnil(L);
    lua_rawseti(L, -2, static_cast<lua_Integer>(hook));
    lua_pop(L, 1);
    return declared;
}

namespace {

// One family's hook table in this VM, created on demand. Leaves the hooks
// root and the family table on the stack (root at -2, family at -1).
void push_family_hook_table(lua_State* L, VmState* st, Order order,
                            int family_id)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->hooks_ref);
    lua_rawgeti(L, -1, hook_table_key(order, family_id));
    if (lua_istable(L, -1))
        return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, hook_table_key(order, family_id));
}

// The per-slot `ai =` sugar, lowered (format spec V2). Upvalue 1 is a
// private table of slot → function; the closure IS the family-level
// check_special_ai, so the engine asks the same question the same number of
// times whichever spelling the pack used. A slot with no opinion answers
// true, which is what the engine does when no hook is registered at all.
int per_special_ai(lua_State* L)
{
    // nil-tolerant: a dispatch always hands over a live walker, but a slot
    // cannot be selected without one, and answering "no opinion" beats
    // raising in the one caller (a test funnel) that has none.
    const walker* self = resolve_walker_or_nil(L, 1);
    const lua_Integer slot =
        (self != nullptr) ? static_cast<lua_Integer>(self->current_special())
                          : 0;
    lua_rawgeti(L, lua_upvalueindex(1), slot);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushvalue(L, 1);  // self
    lua_call(L, 1, 1);
    return 1;
}

// Stores the function on top of the stack as `hook` of the family table at
// `family_idx`, with the same collision report og.register_hooks makes. Pops
// the function.
void bind_one_hook(lua_State* L, VmState* st, int family_idx,
                   const char* order_str, const std::string& family_label,
                   const char* hook_name, FamilyHook hook, Order order,
                   int family_id)
{
    lua_rawgeti(L, family_idx, static_cast<lua_Integer>(hook));
    const bool occupied = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (occupied)
        report_duplicate_hook(L, st, order_str, family_label.c_str(),
                              hook_name);
    if (coverage::enabled())
        coverage_declare_hook(L, std::string(order_str) + "/" + family_label +
                                     "/" + hook_name);
    lua_rawseti(L, family_idx, static_cast<lua_Integer>(hook));
    note_declared_hook(L, family_idx, hook);
    if (st->owner != nullptr)
        st->owner->note_hook(order, family_id, hook);
}

// `specials` + the per-entry `ai` sugar. Both are id-joined against the
// INSTALLED descriptor, so an id the family does not declare is a load
// error naming the ids it does — the same diagnostic og.register_hooks
// gives, for the same mistake.
bool bind_specials(lua_State* L, VmState* st, int tbl, int family_idx,
                   const char* order_str, const std::string& family_label,
                   int family_id)
{
    lua_getfield(L, tbl, "specials");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    const int list = lua_gettop(L);
    const FamilyDescriptor* fd = get_family_descriptor(family_id);
    lua_newtable(L);
    const int casts = lua_gettop(L);
    lua_newtable(L);
    const int ais = lua_gettop(L);
    int cast_count = 0;
    int ai_count = 0;

    const lua_Integer n = luaL_len(L, list);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, list, i);
        const int entry = lua_gettop(L);
        if (!lua_istable(L, entry)) {
            lua_pop(L, 1);
            continue;  // the declaration pass already refused this pack
        }
        lua_getfield(L, entry, "id");
        const char* id = lua_tostring(L, -1);
        if (id == nullptr) {
            lua_pop(L, 2);
            continue;  // the declaration pass already refused this pack
        }
        const std::string special_id = id;
        lua_pop(L, 1);
        const int slot = special_slot_for_id(fd, special_id.c_str());
        if (slot < 0) {
            luaL_error(L,
                       "og.family: special '%s' names no slot of living "
                       "family '%s' — the declaration and the installed "
                       "family disagree (declared ids: %s)",
                       special_id.c_str(), family_label.c_str(),
                       declared_special_ids(fd).c_str());
            return false;  // unreachable
        }
        lua_getfield(L, entry, "cast");
        if (lua_isfunction(L, -1)) {
            if (coverage::enabled())
                coverage_declare_hook(L, std::string(order_str) + "/" +
                                             family_label + "/do_special[" +
                                             std::to_string(slot) + "]");
            lua_rawseti(L, casts, slot);
            cast_count++;
        } else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
            // `cast = false` — the charged no-op, said out loud. It is
            // stored, because the slot being HANDLED is the whole point: a
            // default_cast must not catch it, and the end-of-load check for
            // unhandled castable specials must not flag it.
            lua_rawseti(L, casts, slot);
            cast_count++;
        } else {
            lua_pop(L, 1);
        }
        lua_getfield(L, entry, "ai");
        if (lua_isfunction(L, -1)) {
            if (coverage::enabled())
                coverage_declare_hook(L, std::string(order_str) + "/" +
                                             family_label +
                                             "/check_special_ai[" +
                                             std::to_string(slot) + "]");
            lua_rawseti(L, ais, slot);
            ai_count++;
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);  // entry
    }

    lua_getfield(L, list, "default_cast");
    if (lua_isfunction(L, -1)) {
        if (coverage::enabled())
            coverage_declare_hook(L, std::string(order_str) + "/" +
                                         family_label + "/do_special[default]");
        lua_setfield(L, casts, "default");
        cast_count++;
    } else {
        lua_pop(L, 1);
    }

    if (ai_count > 0) {
        lua_pushvalue(L, ais);
        lua_pushcclosure(L, per_special_ai, 1);
        bind_one_hook(L, st, family_idx, order_str, family_label,
                      "check_special_ai", FamilyHook::CheckSpecialAi,
                      Order::Living, family_id);
    }
    if (cast_count > 0) {
        lua_pushvalue(L, casts);
        bind_one_hook(L, st, family_idx, order_str, family_label, "specials",
                      FamilyHook::DoSpecial, Order::Living, family_id);
    }
    lua_pop(L, 3);  // ais, casts, the declaration's specials list
    return true;
}

// og.family in an ordinary VM. The declaration's data was installed at
// mount; this hangs its behavior in THIS VM.
int bind_family(lua_State* L, VmState* st, const OrderInfo* oi, int tbl)
{
    lua_getfield(L, tbl, "id");
    const char* id = lua_tostring(L, -1);
    if (id == nullptr)
        return luaL_error(L, "og.family %s: a declaration needs an id "
                             "(id = \"<pack>:<family>\")", oi->name);
    const std::string declared_id = id;
    lua_pop(L, 1);
    const int family_id =
        og::families::resolve_family_string_id(oi->order, declared_id.c_str());
    if (family_id < 0) {
        // The chunk declared a family the registries do not have. Either the
        // pack's declaration pass never ran (the caller skips chunks whose
        // pack was rejected) or the install dropped the entry — both are the
        // split-brain case V1 exists to prevent, so say so instead of
        // binding hooks into a void.
        return luaL_error(L,
                          "og.family %s '%s': no such family is installed — "
                          "the declaration that should have created it did "
                          "not survive the install",
                          oi->name, declared_id.c_str());
    }

    push_family_hook_table(L, st, oi->order, family_id);
    const int family_idx = lua_gettop(L);
    for (size_t i = 0; i < oi->hook_count; i++) {
        const char* name = oi->hooks[i].name;
        lua_getfield(L, tbl, name);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        bind_one_hook(L, st, family_idx, oi->name, declared_id, name,
                      oi->hooks[i].hook, oi->order, family_id);
    }
    if (oi->order == Order::Living)
        bind_specials(L, st, tbl, family_idx, oi->name, declared_id,
                      family_id);
    lua_pop(L, 2);  // family table, hooks root
    return 0;
}

// The three entry points share their shape: check the context, harvest, and
// turn a harvest failure into ONE lua error at the call site. Raising from
// here rather than deep in the walk is what lets the walk be ordinary C++
// with ordinary destructors.
int raise_harvest_error(lua_State* L, const Harvest& h)
{
    return luaL_error(L, "%s", h.err.c_str());
}

// og.family / og.anims / og.pack all describe DATA, and data installs from
// families/ (format spec V1). Elsewhere a declaration would bind behavior in
// every VM while its data half never installed — the split-brain family this
// whole two-context design exists to make impossible.
bool require_families_chunk(lua_State* L, const VmState* st, const char* what)
{
    if (st != nullptr && st->current_chunk == ChunkKind::Family)
        return true;
    luaL_error(L,
               "%s: only a packs/<id>/families/*.lua chunk may declare pack "
               "data — families/ is what the installer evaluates, so a "
               "declaration anywhere else would never install",
               what);
    return false;  // unreachable; luaL_error does not return
}

}  // namespace

int og_family(lua_State* L)
{
    const char* order_str = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    VmState* st = get_vm_state(L);
    if (!require_families_chunk(L, st, "og.family"))
        return 0;
    const OrderInfo* oi = find_order(order_str);
    if (oi == nullptr)
        return luaL_error(L, "og.family: unknown order '%s' (living, weapon, "
                             "effect, treasure, generator)", order_str);
    if (st->mode != VmMode::Declare) {
        // BIND pass: the same call binds hooks and specials casts against
        // the descriptors this declaration already installed, and touches no
        // registry.
        return bind_family(L, st, oi, 2);
    }
    if (st->harvest == nullptr)
        return luaL_error(L, "og.family: no declaration in progress");
    Harvest h{L, {}};
    // Argument 2 is the declaration table; every reader indexes it directly
    // rather than copying, so the caller's stack is untouched on the way
    // out (harvest errors longjmp past any cleanup we might otherwise want).
    if (!harvest_entry(h, oi, 2, *st->harvest))
        return raise_harvest_error(L, h);
    return 0;
}

int og_anims(lua_State* L)
{
    size_t name_len = 0;
    const char* name = luaL_checklstring(L, 1, &name_len);
    luaL_checktype(L, 2, LUA_TTABLE);
    VmState* st = get_vm_state(L);
    if (!require_families_chunk(L, st, "og.anims"))
        return 0;
    if (st->mode != VmMode::Declare)
        return 0;  // frame tables are data; the bind replay wants none of it
    if (st->harvest == nullptr)
        return luaL_error(L, "og.anims: no declaration in progress");
    Harvest h{L, {}};
    og::data::ClasspackAnimSet set;
    if (!harvest_anim_set(h, std::string(name, name_len), 2, set))
        return raise_harvest_error(L, h);
    st->harvest->anims.push_back(std::move(set));
    return 0;
}

int og_pack(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    VmState* st = get_vm_state(L);
    if (!require_families_chunk(L, st, "og.pack"))
        return 0;
    if (st->mode != VmMode::Declare)
        return 0;
    if (st->harvest == nullptr)
        return luaL_error(L, "og.pack: no declaration in progress");
    Harvest h{L, {}};
    if (!harvest_pack_header(h, 1, *st->harvest))
        return raise_harvest_error(L, h);
    return 0;
}

// ---------------------------------------------------------------------------
// Packs whose declaration pass FAILED
// ---------------------------------------------------------------------------

namespace {

std::set<std::string>& failed_declarations()
{
    static auto* failed = new std::set<std::string>();
    return *failed;
}

// order*4096 + family_id → the pack that declared the slot in Lua. Same key
// shape as the per-VM hook tables, for the same reason: one flat map over
// (order, family byte).
std::map<int, std::string>& lua_declared_families()
{
    static auto* declared = new std::map<int, std::string>();
    return *declared;
}

}  // namespace

void clear_failed_pack_declarations()
{
    failed_declarations().clear();
}

void note_failed_pack_declaration(const std::string& pack_id)
{
    failed_declarations().insert(pack_id);
}

bool pack_declaration_failed(const std::string& pack_id)
{
    return failed_declarations().count(pack_id) != 0;
}

void clear_lua_declared_families()
{
    lua_declared_families().clear();
}

void note_lua_declared_family(Order order, int family_id,
                              const std::string& pack_id)
{
    lua_declared_families()[static_cast<int>(order) * 4096 + family_id] =
        pack_id;
}

const std::string* lua_declared_family_pack(Order order, int family_id)
{
    const auto& map = lua_declared_families();
    const auto hit = map.find(static_cast<int>(order) * 4096 + family_id);
    return hit == map.end() ? nullptr : &hit->second;
}

// ---------------------------------------------------------------------------
// The declaration pass
// ---------------------------------------------------------------------------

DeclareResult declare_pack_families(const std::string& pack_id,
                                    og::data::ClasspackData& out)
{
    // A pack with no family chunks never pays for any of this: no VM.
    bool any = false;
    for (const PackScript& chunk : pack_family_chunks()) {
        if (chunk.pack_id == pack_id) {
            any = true;
            break;
        }
    }
    if (!any)
        return {true, {}};

    // A FRESH, TIGHT VM. Fresh because a declaration must not see anything a
    // previous pack left behind; thrown away at the end of the call because
    // everything worth keeping has been copied into `out` as plain C++.
    // The VmState is declared FIRST so it outlives the host: the fence
    // closures hold its address as an upvalue and the Lua registry holds it
    // as lightuserdata, and lua_close runs __gc metamethods that could still
    // reach a binding.
    VmState st;
    ScriptHost host;
    ScriptHost::Impl& impl = host.impl();
    st.mode = VmMode::Declare;
    st.harvest = &out;
    install_vm_scaffolding(impl, st);
    // The whole pass is chunk top level, so the world API is closed for all
    // of it — a declaration that read the world or drew from the RNG would
    // make the installed registry a function of one peer's game state.
    st.loading = true;
    st.current_pack = pack_id;
    st.current_chunk = ChunkKind::Family;

    DeclareResult result{true, {}};
    for (const PackScript& chunk : pack_family_chunks()) {
        if (chunk.pack_id != pack_id)
            continue;
        if (host.run_chunk(chunk.chunk_name, chunk.source, chunk.pack_id))
            continue;
        // One unusable family file rejects the whole pack. Half a pack is
        // a game whose families depend on load order.
        const auto& errors = host.errors();
        result.ok = false;
        result.error = errors.empty() ? std::string("chunk '") +
                                            chunk.chunk_name +
                                            "' failed to load"
                                      : errors.back().message;
        break;
    }
    st.current_chunk = ChunkKind::None;
    st.current_pack.clear();
    st.loading = false;
    st.harvest = nullptr;
    return result;
}

}  // namespace og::script
