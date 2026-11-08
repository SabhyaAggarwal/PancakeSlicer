#pragma once

#include <cstddef>
#include <wx/accel.h>
#include <wx/defs.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxKeyEvent;

namespace Slic3r { namespace GUI {

struct KeyChord
{
    int key       = WXK_NONE;
    int modifiers = wxMOD_NONE;

    bool valid() const { return key != WXK_NONE; }
    bool operator==(const KeyChord& other) const { return key == other.key && modifiers == other.modifiers; }
    bool operator!=(const KeyChord& other) const { return !(*this == other); }

    bool is_punctuation() const;
    bool needs_char_event() const;
    bool is_menu_accelerator() const;
    bool is_system_shortcut() const;

    std::string to_string() const;
    static std::optional<KeyChord> parse(const std::string& text);

    std::string display() const;
    std::vector<std::string> display_parts() const;
    static std::string modifier_prefix(int modifier);
    static std::string modifier_name(int modifier);

    wxAcceleratorEntry to_accelerator_entry(int command) const;

    static KeyChord from_event(const wxKeyEvent& evt);
};

struct KeyChordHash
{
    size_t operator()(const KeyChord& chord) const { return std::hash<long long>()((static_cast<long long>(chord.modifiers) << 32) | unsigned(chord.key)); }
};

}}
