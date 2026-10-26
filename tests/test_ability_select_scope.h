#pragma once

// ScopedAbilitySelect: pins the New Ability Select setting (cfg
// effects/new_ability_select AND its cached copy, og::input::ability_select_on)
// for one scope and puts both back exactly as they were.
//
// The integration harness runs with the setting ON, the shipped default
// (integration_main.cpp re-stamps it before every test). A test that pins
// output only the classic game produces takes the scope:
//
//     ScopedAbilitySelect off;            // the setting off for this scope
//     ScopedAbilitySelect on(true);       // or pin it on explicitly
//
// Writes only the in-memory cfg (never save_settings()), so nothing reaches
// cfg/openglad.yaml.

#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/resources/gparser.h>

#include <map>
#include <optional>
#include <string>

class ScopedAbilitySelect
{
public:
    explicit ScopedAbilitySelect(bool enabled = false)
        : saved_value_(stored_value()),
          saved_cache_(input_hardware_state().ability_select)
    {
        cfg.apply_setting("effects", "new_ability_select", enabled ? "on" : "off");
        og::input::refresh_ability_select_cache();
    }

    ~ScopedAbilitySelect()
    {
        if (saved_value_.has_value())
            cfg.apply_setting("effects", "new_ability_select", *saved_value_);
        else
        {
            const auto category = cfg.data.find("effects");
            if (category != cfg.data.end())
                category->second.erase("new_ability_select");
        }
        input_hardware_state().ability_select = saved_cache_;
    }

    ScopedAbilitySelect(const ScopedAbilitySelect&) = delete;
    ScopedAbilitySelect& operator=(const ScopedAbilitySelect&) = delete;

private:
    static std::optional<std::string> stored_value()
    {
        const auto category = cfg.data.find("effects");
        if (category == cfg.data.end())
            return std::nullopt;
        const auto entry = category->second.find("new_ability_select");
        if (entry == category->second.end())
            return std::nullopt;
        return entry->second;
    }

    std::optional<std::string> saved_value_;
    bool saved_cache_;
};
