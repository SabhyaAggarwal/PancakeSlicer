#include "HintNotification.hpp"
#include "ImGuiWrapper.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "GLCanvas3D.hpp"
#include "MainFrame.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cstdint>
#include "libslic3r/Exception.hpp"
#include <cereal/cereal.hpp>
#include <imgui.h>
#include <boost/filesystem/path.hpp>
#include <ios>
#include <exception>
#include <boost/filesystem/operations.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <boost/algorithm/string/predicate.hpp>
#include <functional>
#include <boost/nowide/convert.hpp>
#include <cassert>
#include <map>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <vector>
#include <string>
#include "slic3r/GUI/GUI_App.hpp"
#include <utility>
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/GUI.hpp"

#define HINTS_CEREAL_VERSION 1
struct HintsCerealData
{
std::vector<std::string> my_data;
template<class Archive>
void serialize(Archive& ar, std::uint32_t const version)
{
if (version > HINTS_CEREAL_VERSION)
	throw Slic3r::IOError("Version of hints.cereal is higher than current version.");
else
	ar(my_data);
}
};
CEREAL_CLASS_VERSION(HintsCerealData, HINTS_CEREAL_VERSION);

namespace Slic3r {
namespace GUI {

const std::string BOLD_MARKER_START = "<b>";
const std::string BOLD_MARKER_END = "</b>";
const std::string HYPERTEXT_MARKER_START = "<a>";
const std::string HYPERTEXT_MARKER_END = "</a>";

namespace {
	inline void push_style_color(ImGuiCol idx, const ImVec4& col, bool fading_out, float current_fade_opacity)
	{
		if (fading_out)
			ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w * current_fade_opacity));
		else
			ImGui::PushStyleColor(idx, col);
	}

	void write_used_binary(const std::vector<std::string>& ids)
	{
		boost::nowide::ofstream file((boost::filesystem::path(data_dir()) / "user" / "hints.cereal").string(), std::ios::binary);
		cereal::BinaryOutputArchive archive(file);
		HintsCerealData cd{ ids };
		try
		{
			archive(cd);
		}
		catch (const std::exception& ex)
		{
			BOOST_LOG_TRIVIAL(error) << "Failed to write to hints.cereal. " << ex.what();
		}
	}
	void read_used_binary(std::vector<std::string>& ids)
	{
		boost::filesystem::path path(boost::filesystem::path(data_dir()) / "user" / "hints.cereal");
		if (!boost::filesystem::exists(path)) {
			BOOST_LOG_TRIVIAL(warning) << "Failed to load to hints.cereal. File does not exists. " << path.string();
			return;
		}
		boost::nowide::ifstream file(path.string());
		cereal::BinaryInputArchive archive(file);
		HintsCerealData cd;
		try
		{
			archive(cd);
		}
		catch (const std::exception& ex)
		{
			BOOST_LOG_TRIVIAL(error) << "Failed to load to hints.cereal. " << ex.what();
			return;
		}
		ids = cd.my_data;
	}
	enum TagCheckResult
	{
		TagCheckAffirmative,
		TagCheckNegative,
		TagCheckNotCompatible
	};
	TagCheckResult tag_check_mode(const std::string& tag)
	{
		std::vector<std::string> allowed_tags = { "simple", "advanced", "expert" };
		if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end())
		{
			ConfigOptionMode config_mode = wxGetApp().get_mode();
			if (config_mode == ConfigOptionMode::comSimple)        return (tag == "simple" ? TagCheckAffirmative : TagCheckNegative);
			else if (config_mode == ConfigOptionMode::comAdvanced) return (tag == "advanced" ? TagCheckAffirmative : TagCheckNegative);
			else if (config_mode == ConfigOptionMode::comExpert || config_mode == ConfigOptionMode::comDevelop)
				return (tag == "expert" ? TagCheckAffirmative : TagCheckNegative);
		}
		return TagCheckNotCompatible;
	}

	TagCheckResult tag_check_tech(const std::string& tag)
	{
		std::vector<std::string> allowed_tags = { "FFF", "MMU", "SLA" };
		if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end()) {
			const PrinterTechnology tech = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
			if (tech == ptFFF) {
				bool is_mmu = wxGetApp().extruders_edited_cnt() > 1;
				if (tag == "MMU") return (is_mmu ? TagCheckAffirmative : TagCheckNegative);
				return (tag == "FFF" ? TagCheckAffirmative : TagCheckNegative);
			}
			else {
				return (tag == "SLA" ? TagCheckAffirmative : TagCheckNegative);
			}
		}
		return TagCheckNotCompatible;
	}

	TagCheckResult tag_check_system(const std::string& tag)
	{
		std::vector<std::string> allowed_tags = { "Windows", "Linux", "OSX" };
		if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end()) {
			if (tag == "Windows")
#ifdef WIN32
				return TagCheckAffirmative;
#else 
				return TagCheckNegative;
#endif

			if (tag == "Linux")
#ifdef __linux__
				return TagCheckAffirmative;
#else 
				return TagCheckNegative;
#endif

			if (tag == "OSX")
#ifdef __APPLE__
				return TagCheckAffirmative;
#else 
				return TagCheckNegative;
#endif
		}
		return TagCheckNotCompatible;
	}

	TagCheckResult tag_check_material(const std::string& tag)
	{
		if (const GUI::Tab* tab = wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)) {
			if (wxGetApp().app_config->get("filament_type").find(tag)) {
				const Preset& preset = tab->m_presets->get_edited_preset();
				const auto* opt = preset.config.opt<ConfigOptionStrings>("filament_type");
				if (opt->values[0] == tag)
					return TagCheckAffirmative;
				return TagCheckNegative;
			}
			return TagCheckNotCompatible;
		}
		return TagCheckNotCompatible;
	}

	bool tags_check(const std::string& disabled_tags, const std::string& enabled_tags)
	{
		if (disabled_tags.empty() && enabled_tags.empty())
			return true;
		if (!enabled_tags.empty()) {
			std::string tag;
			for (size_t i = 0; i < enabled_tags.size(); i++) {
				if (enabled_tags[i] == ' ') {
					tag.erase();
					continue;
				}
				if (enabled_tags[i] != ';') {
					tag += enabled_tags[i];
				}
				if (enabled_tags[i] == ';' || i == enabled_tags.size() - 1) {
					if (!tag.empty()) {
						TagCheckResult result;
						result = tag_check_mode(tag);
						if (result == TagCheckResult::TagCheckNegative)
							return false;
						if (result == TagCheckResult::TagCheckAffirmative)
							continue;
						result = tag_check_tech(tag);
						if (result == TagCheckResult::TagCheckNegative)
							return false;
						if (result == TagCheckResult::TagCheckAffirmative)
							continue;
						result = tag_check_system(tag);
						if (result == TagCheckResult::TagCheckNegative)
							return false;
						if (result == TagCheckResult::TagCheckAffirmative)
							continue;
						result = tag_check_material(tag);
						if (result == TagCheckResult::TagCheckNegative)
							return false;
						if (result == TagCheckResult::TagCheckAffirmative)
							continue;
						BOOST_LOG_TRIVIAL(error) << "Hint Notification: Tag " << tag << " in enabled_tags not compatible.";
						return false;
					}
				}
			}
		}
		if (!disabled_tags.empty()) {
			std::string tag;
			for (size_t i = 0; i < disabled_tags.size(); i++) {
				if (disabled_tags[i] == ' ') {
					tag.erase();
					continue;
				}
				if (disabled_tags[i] != ';') {
					tag += disabled_tags[i];
				}
				if (disabled_tags[i] == ';' || i == disabled_tags.size() - 1) {
					if (!tag.empty()) {
						TagCheckResult result;
						result = tag_check_mode(tag);
						if (result == TagCheckResult::TagCheckNegative)
							continue;
						if (result == TagCheckResult::TagCheckAffirmative)
							return false;
						result = tag_check_tech(tag);
						if (result == TagCheckResult::TagCheckNegative)
							continue;
						if (result == TagCheckResult::TagCheckAffirmative)
							return false;
						result = tag_check_system(tag);
						if (result == TagCheckResult::TagCheckAffirmative)
							return false;
						if (result == TagCheckResult::TagCheckNegative)
							continue;
						result = tag_check_material(tag);
						if (result == TagCheckResult::TagCheckAffirmative)
							return false;
						if (result == TagCheckResult::TagCheckNegative)
							continue;
						BOOST_LOG_TRIVIAL(error) << "Hint Notification: Tag " << tag << " in disabled_tags not compatible.";
					}
				}
			}
		}
		return true;
	}
	void launch_browser_if_allowed(const std::string& url)
	{
		wxGetApp().open_browser_with_warning_dialog(url);
	}
}
HintDatabase::~HintDatabase()
{
	if (m_initialized) {
		write_used_binary(m_used_ids);
	}
}
void HintDatabase::uninit()
{
	if (m_initialized) {
		write_used_binary(m_used_ids);
	}
	m_initialized = false;
	m_loaded_hints.clear();
	m_sorted_hints = false;
	m_used_ids.clear();
	m_used_ids_loaded = false;
}
void HintDatabase::reinit()
{
	if (m_initialized)
		uninit();
	init();
}
void HintDatabase::init()
{
	load_hints_from_file(std::move(boost::filesystem::path(resources_dir()) / "data" / "hints.ini"));
	m_initialized = true;
	init_random_hint_id();
}
void HintDatabase::init_random_hint_id()
{
	srand(time(NULL));
	m_hint_id = rand() % m_loaded_hints.size();
}
void HintDatabase::load_hints_from_file(const boost::filesystem::path& path)
{
	namespace pt = boost::property_tree;
	pt::ptree tree;
	boost::nowide::ifstream ifs(path.string());
	try {
		pt::read_ini(ifs, tree);
	}
	catch (const boost::property_tree::ini_parser::ini_parser_error& err) {
		throw Slic3r::RuntimeError(format("Failed loading hints file \"%1%\"\nError: \"%2%\" at line %3%", path, err.message(), err.line()).c_str());
	}

	for (const auto& section : tree) {
		if (boost::starts_with(section.first, "hint:")) {
			std::map<std::string, std::string> dict;
			for (const auto& data : section.second) {
				dict.emplace(data.first, data.second.data());
			}
			std::string id_string = section.first.substr(5);
			id_string = std::to_string(std::hash<std::string>{}(id_string));
			std::string fulltext;
			std::string text1;
			std::string hypertext_text;
			std::string follow_text;
			std::string disabled_tags;
			std::string enabled_tags;
			std::string documentation_link;
			std::string img_url;
			size_t      weight = 1;
			bool		was_displayed = is_used(id_string);
			unescape_string_cstyle(dict["text"], fulltext);
			fulltext = _utf8(fulltext);
#ifdef __APPLE__
			boost::replace_all(fulltext, "Ctrl+", "?");
#endif
			std::string marker_s(1, ImGui::ColorMarkerStart);
			std::string marker_e(1, ImGui::ColorMarkerEnd);
			size_t marker_pos = fulltext.find(BOLD_MARKER_START);
			while (marker_pos != std::string::npos) {
				fulltext.replace(marker_pos, 3, marker_s);
				marker_pos = fulltext.find(BOLD_MARKER_START, marker_pos);
			}
			marker_pos = fulltext.find(BOLD_MARKER_END);
			while (marker_pos != std::string::npos) {
				fulltext.replace(marker_pos, 4, marker_e);
				marker_pos = fulltext.find(BOLD_MARKER_END, marker_pos);
			}
			size_t hypertext_start = fulltext.find(HYPERTEXT_MARKER_START);
			if (hypertext_start != std::string::npos) {
				fulltext.erase(hypertext_start, HYPERTEXT_MARKER_START.size());
				if (fulltext.find(HYPERTEXT_MARKER_START) != std::string::npos) {
					BOOST_LOG_TRIVIAL(error) << "Hint notification with multiple hypertexts: " << _utf8(dict["text"]);
					continue;
				}
				size_t hypertext_end = fulltext.find(HYPERTEXT_MARKER_END);
				if (hypertext_end == std::string::npos) {
					BOOST_LOG_TRIVIAL(error) << "Hint notification without hypertext end marker: " << _utf8(dict["text"]);
					continue;
				}
				fulltext.erase(hypertext_end, HYPERTEXT_MARKER_END.size());
				if (fulltext.find(HYPERTEXT_MARKER_END) != std::string::npos) {
					BOOST_LOG_TRIVIAL(error) << "Hint notification with multiple hypertext end markers: " << _utf8(dict["text"]);
					continue;
				}

				text1 = fulltext.substr(0, hypertext_start);
				hypertext_text = fulltext.substr(hypertext_start, hypertext_end - hypertext_start);
				follow_text = fulltext.substr(hypertext_end);
			}
			else {
				text1 = fulltext;
			}

			if (dict.find("disabled_tags") != dict.end()) {
				disabled_tags = dict["disabled_tags"];
			}
			if (dict.find("enabled_tags") != dict.end()) {
				enabled_tags = dict["enabled_tags"];
			}
			if (dict.find("documentation_link") != dict.end()) {
				documentation_link = dict["documentation_link"];
			}
			if (dict.find("image") != dict.end()) {
				img_url = dict["image"];
			}

			if (dict.find("weight") != dict.end()) {
				weight = (size_t)std::max(1, std::atoi(dict["weight"].c_str()));
			}

			if (dict.find("hypertext_type") != dict.end()) {
				if (dict["hypertext_type"] == "link") {
					std::string	hypertext_link = dict["hypertext_link"];
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, img_url, [hypertext_link]() { launch_browser_if_allowed(hypertext_link); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "settings") {
					std::string		opt = dict["hypertext_settings_opt"];
					Preset::Type	type = static_cast<Preset::Type>(std::atoi(dict["hypertext_settings_type"].c_str()));
					std::wstring	category = boost::nowide::widen(dict["hypertext_settings_category"]);
					HintData		hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, img_url, [opt, type, category]() { GUI::wxGetApp().sidebar().jump_to_option(opt, type, category); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "preferences") {
					std::string	item = dict["hypertext_preferences_item"];
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, img_url, [item]() { wxGetApp().open_preferences(PreferencesTab::Control, item); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "plater") {
					std::string	item = dict["hypertext_plater_item"];
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, img_url, [item]() { wxGetApp().plater()->canvas3D()->highlight_toolbar_item(item); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "gizmo") {
					std::string	item = dict["hypertext_gizmo_item"];
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, img_url, [item]() { wxGetApp().plater()->canvas3D()->highlight_gizmo(item); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "gallery") {
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, img_url, []() {
						wxGetApp().plater()->canvas3D()->deselect_all();
					} };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "menubar") {
					wxString menu(_("&" + dict["hypertext_menubar_menu_name"]));
					wxString item(_(dict["hypertext_menubar_item_name"]));
					HintData	hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, img_url, [menu, item]() { wxGetApp().mainframe->open_menubar_item(menu, item); } };
					m_loaded_hints.emplace_back(hint_data);
				}
			}
			else {
				HintData hint_data{ id_string, text1, weight, was_displayed, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, img_url };
				m_loaded_hints.emplace_back(hint_data);
			}
		}
	}
}
HintData* HintDatabase::get_hint(HintDataNavigation nav)
{
	if (!m_initialized) {
		init();
		nav = HintDataNavigation::Random;
	}
	if (m_loaded_hints.empty())
	{
		BOOST_LOG_TRIVIAL(error) << "There were no hints loaded from hints.ini file.";
		return nullptr;
	}

	try
	{
		if (nav == HintDataNavigation::Next)
			m_hint_id = get_next_hint_id();
		if(nav == HintDataNavigation::Prev)
			m_hint_id = get_prev_hint_id();
		if (nav == HintDataNavigation::Random)
			init_random_hint_id();
	}
	catch (const std::exception&)
	{
		return nullptr;
	}

	return &m_loaded_hints[m_hint_id];
}

size_t HintDatabase::get_next_hint_id()
{
	return m_hint_id < m_loaded_hints.size() - 1 ? m_hint_id + 1 : 0;
}

size_t HintDatabase::get_prev_hint_id()
{
	return m_hint_id > 0 ? m_hint_id - 1 : m_loaded_hints.size() - 1;
}

size_t HintDatabase::get_random_next()
{
	if (!m_sorted_hints)
	{
		auto compare_wieght = [](const HintData& a, const HintData& b) { return a.weight < b.weight; };
		std::sort(m_loaded_hints.begin(), m_loaded_hints.end(), compare_wieght);
		m_sorted_hints = true;
		srand(time(NULL));
	}
	std::vector<size_t> candidates;
	size_t total_weight = 0;
	for (size_t i = 0; i < m_loaded_hints.size(); i++) {
		if (!m_loaded_hints[i].was_displayed && tags_check(m_loaded_hints[i].disabled_tags, m_loaded_hints[i].enabled_tags)) {
			candidates.emplace_back(i);
			total_weight += m_loaded_hints[i].weight;
		}
	}
	if (total_weight == 0) {
		clear_used();
		for (size_t i = 0; i < m_loaded_hints.size(); i++) {
			m_loaded_hints[i].was_displayed = false;
			if (tags_check(m_loaded_hints[i].disabled_tags, m_loaded_hints[i].enabled_tags)) {
				candidates.emplace_back(i);
				total_weight += m_loaded_hints[i].weight;
			}
		}
	}
	if (total_weight == 0) {
		BOOST_LOG_TRIVIAL(error) << "Hint notification random number generator failed. No suitable hint was found.";
		throw std::exception();
	}
	size_t random_number = rand() % total_weight + 1;
	size_t current_weight = 0;
	for (size_t i = 0; i < candidates.size(); i++) {
		current_weight += m_loaded_hints[candidates[i]].weight;
		if (random_number <= current_weight) {
			set_used(m_loaded_hints[candidates[i]].id_string);
			m_loaded_hints[candidates[i]].was_displayed = true;
			return candidates[i];
		}
	}
	BOOST_LOG_TRIVIAL(error) << "Hint notification random number generator failed.";
	throw std::exception();
}

bool HintDatabase::is_used(const std::string& id)
{
	if (!m_used_ids_loaded) {
		read_used_binary(m_used_ids);
		m_used_ids_loaded = true;
	}
	for (const std::string& used_id : m_used_ids) {
		if (used_id == id)
		{
			return true;
		}
	}
	return false;
}
void HintDatabase::set_used(const std::string& id)
{
	if (!is_used(id))
	{
		m_used_ids.emplace_back(id);
	}
}
void HintDatabase::clear_used()
{
	m_used_ids.clear();
}

void NotificationManager::HintNotification::count_spaces()
{
	m_line_height = ImGui::CalcTextSize("A").y;


	std::string text;
	text = ImGui::WarningMarker;
	float picture_width = ImGui::CalcTextSize(text.c_str()).x;
	m_left_indentation = picture_width * 1.5f + m_line_height / 2;

	if (m_documentation_link.empty())
		m_window_width_offset = m_left_indentation + m_line_height * 3.f;
	else
		m_window_width_offset = m_left_indentation + m_line_height * 5.5f;

	m_window_width = m_line_height * 25;
}

static int get_utf8_seq_length(const char* seq, size_t size)
{
	int length = 0;
	unsigned char c = seq[0];
	if (c < 0x80) {
		length++;
	}
	else if (c < 0xC0) {
		length++;
		while (length < size) {
			c = seq[length];
			if (c < 0x80 || c >= 0xC0) {
				break;
			}
			length++;
		}
	}
	else if (c < 0xE0) {
		if (2 > size) {
			return size;
		}
		length += 2;
	}
	else if (c < 0xF0) {
		if (3 > size) {
			return size;
		}
		length += 3;
	}
	else if (c < 0xF8) {
		if (4 > size) {
			return size;
		}
		length += 4;
	}
	else if (c < 0xFC) {
		if (5 > size) {
			return size;
		}
		length += 5;
	}
	else if (c < 0xFE) {
		if (6 > size) {
			return size;
		}
		length += 6;
	}
	else {
		length++;
	}
	return length;
}

static int get_utf8_seq_length(const std::string& text, size_t pos)
{
	assert(pos < text.size());
	return get_utf8_seq_length(text.c_str() + pos, text.size() - pos);
}

void NotificationManager::HintNotification::count_lines()
{
	std::string text = m_text1;
	size_t      last_end = 0;
	m_lines_count = 0;

	if (text.empty())
		return;

	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		size_t next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		}
		else {
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset) {
				size_t next_space = text.find_first_of(' ', last_end);
				if (next_space > 0 && next_space < text.length()) {
					size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
					while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset) {
						next_space = next_space_candidate;
						next_space_candidate = text.find_first_of(' ', next_space + 1);
					}
				}
				else {
					next_space = text.length();
				}
				if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset ||
					ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x < (m_window_width - m_window_width_offset) / 5 * 3
					) {
					float width_of_a = ImGui::CalcTextSize("a").x;
					int letter_count = (int)((m_window_width - m_window_width_offset) / width_of_a);
					while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset) {
						letter_count += get_utf8_seq_length(text, last_end + letter_count);
					}
					m_endlines.push_back(last_end + letter_count);
					last_end += letter_count;
				}
				else {
					m_endlines.push_back(next_space);
					last_end = next_space + 1;
				}
			}
			else {
				m_endlines.push_back(text.length());
				last_end = text.length();
			}

		}
		m_lines_count++;
	}
	int prev_end = m_endlines.size() > 1 ? m_endlines[m_endlines.size() - 2] : 0;
	int size_of_last_line = ImGui::CalcTextSize(text.substr(prev_end, last_end - prev_end).c_str()).x;
	if (!m_hypertext.empty()) {
		if (size_of_last_line + ImGui::CalcTextSize(m_hypertext.c_str()).x > m_window_width - m_window_width_offset) {
			size_of_last_line = ImGui::CalcTextSize((m_hypertext + "  ").c_str()).x;
			m_endlines.push_back(last_end);
			m_lines_count++;
		}
		else {
			size_of_last_line += ImGui::CalcTextSize((m_hypertext + "  ").c_str()).x;
		}
	}
	if (!m_text2.empty()) {
		text = m_text2;
		last_end = 0;
		m_endlines2.clear();
		size_t first_end = std::min(text.find_first_of('\n'), text.find_first_of(' '));
		if (size_of_last_line >= m_window_width - m_window_width_offset - ImGui::CalcTextSize(text.substr(0, first_end).c_str()).x) {
			m_endlines2.push_back(0);
			size_of_last_line = 0;
		}
		while (last_end < text.length() - 1)
		{
			size_t next_hard_end = text.find_first_of('\n', last_end);
			if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
				m_endlines2.push_back(next_hard_end);
				last_end = next_hard_end + 1;
			}
			else {
				if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset - size_of_last_line) {
					size_t next_space = text.find_first_of(' ', last_end);
					if (next_space > 0) {
						size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
						while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
							next_space = next_space_candidate;
							next_space_candidate = text.find_first_of(' ', next_space + 1);
						}
					}
					else {
						next_space = text.length();
					}
					if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset - size_of_last_line ||
						ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x + size_of_last_line < (m_window_width - m_window_width_offset) / 5 * 3
						) {
						float width_of_a = ImGui::CalcTextSize("a").x;
						int letter_count = (int)((m_window_width - m_window_width_offset - size_of_last_line) / width_of_a);
						while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
							letter_count += get_utf8_seq_length(text, last_end + letter_count);
						}
						m_endlines2.push_back(last_end + letter_count);
						last_end += letter_count;
					}
					else {
						m_endlines2.push_back(next_space);
						last_end = next_space + 1;
					}
				}
				else {
					m_endlines2.push_back(text.length());
					last_end = text.length();
				}

			}
			if (size_of_last_line == 0)
				m_lines_count++;
			size_of_last_line = 0;

		}
	}
}

void NotificationManager::HintNotification::init()
{
	if (is_finished())
		return;

	count_spaces();
	count_lines();

	m_multiline = true;

	m_notification_start = GLCanvas3D::timestamp_now();
	if (m_state == EState::Unknown)
		m_state = EState::Shown;
}

void NotificationManager::HintNotification::set_next_window_size(ImGuiWrapper& imgui)
{

	m_window_height = std::max((m_lines_count + 1.f) * m_line_height, 5.f * m_line_height);
}

bool NotificationManager::HintNotification::on_text_click()
{
	if (m_hypertext_callback != nullptr && (!m_runtime_disable || tags_check(m_disabled_tags, m_enabled_tags)))
		m_hypertext_callback();
	return false;
}

void NotificationManager::HintNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	if (!m_has_hint_data) {
		retrieve_data();
	}

	float	x_offset = m_left_indentation;
	int		last_end = 0;
	float	starting_y = ( m_line_height / 2);
	float	shift_y = m_line_height;
	std::string line;

	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_HyperTextColor);
	for (size_t i = 0; i < (m_multiline ? m_endlines.size() : 2); i++) {
		line.clear();
		ImGui::SetCursorPosX(x_offset);
		ImGui::SetCursorPosY(starting_y + i * shift_y);
		if (m_endlines.size() > i && m_text1.size() >= m_endlines[i]) {
			if (i == 1 && m_endlines.size() > 2 && !m_multiline) {
				line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0), m_endlines[1] - m_endlines[0] - (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
				while (ImGui::CalcTextSize(line.c_str()).x > m_window_width - m_window_width_offset - ImGui::CalcTextSize((".." + _u8L("More")).c_str()).x) {
					line = line.substr(0, line.length() - 1);
				}
				line += "..";
			}
			else {
				line = m_text1.substr(last_end, m_endlines[i] - last_end);
			}
			if (m_text1.find('\n') >= m_endlines[i]) {
				line = ImGui::ColorMarkerStart + line + ImGui::ColorMarkerEnd;
			}
			if (line.find_first_of(ImGui::ColorMarkerEnd) < line.find_first_of(ImGui::ColorMarkerStart)) {
				line = ImGui::ColorMarkerStart + line;
			}

			last_end = m_endlines[i];
			if (m_text1.size() > m_endlines[i])
				last_end += (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
			imgui.text(line.c_str());
		}

	}
	if (!m_multiline && m_lines_count > 2) {
		render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + " ").c_str()).x, starting_y + shift_y, _u8L("More"), true);
	}
	else if (!m_hypertext.empty()) {
		render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + (line.empty() ? "" : " ")).c_str()).x, starting_y + (m_endlines.size() - 1) * shift_y, m_hypertext);
	}

	if (!m_text2.empty() && m_multiline) {
		starting_y += (m_endlines.size() - 1) * shift_y;
		last_end = 0;
		for (size_t i = 0; i < (m_multiline ? m_endlines2.size() : 2); i++) {
			if (i == 0)
				ImGui::SetCursorPosX(x_offset + ImGui::CalcTextSize((line + m_hypertext + (line.empty() ? " " : "  ")).c_str()).x);
			else
				ImGui::SetCursorPosX(x_offset);

			ImGui::SetCursorPosY(starting_y + i * shift_y);
			line.clear();
			if (m_endlines2.size() > i && m_text2.size() >= m_endlines2[i]) {

				line = m_text2.substr(last_end, m_endlines2[i] - last_end);

				if (line.find_first_of(ImGui::ColorMarkerEnd) < line.find_first_of(ImGui::ColorMarkerStart)) {
					line = ImGui::ColorMarkerStart + line;
				}

				last_end = m_endlines2[i];
				if (m_text2.size() > m_endlines2[i])
					last_end += (m_text2[m_endlines2[i]] == '\n' || m_text2[m_endlines2[i]] == ' ' ? 1 : 0);
				imgui.text(line.c_str());
			}

		}
	}
	ImGui::PopStyleColor(1);
}

void NotificationManager::HintNotification::render_close_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));


	std::wstring button_text;
	button_text = m_is_dark ? ImGui::CloseNotifDarkButton : ImGui::CloseNotifButton;

	ImVec2 button_pic_size = ImGui::CalcTextSize(into_u8(button_text).c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	m_close_b_w = button_size.y;
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - win_size.x / 10.f, win_pos.y + win_size.y / 2 - button_pic_size.y),
		ImVec2(win_pos.x, win_pos.y + win_size.y / 2 + button_pic_size.y),
		true))
	{
		button_text = m_is_dark ? ImGui::CloseNotifHoverDarkButton : ImGui::CloseNotifHoverButton;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			close();
	}
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.75f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		close();
	}


	ImGui::PopStyleColor(5);


	render_right_arrow_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	render_preferences_button(imgui, win_pos_x, win_pos_y);
	if (!m_documentation_link.empty() && wxGetApp().app_config->get("suppress_hyperlinks") != "1")
	{
		render_documentation_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	}

}

void NotificationManager::HintNotification::render_preferences_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y)
{
	auto scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);

	std::wstring button_text;
	button_text = m_is_dark ? ImGui::PreferencesDarkButton : ImGui::PreferencesButton;
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos_x - m_window_width / 15.f, win_pos_y + m_window_height - 1.5f * m_line_height),
		ImVec2(win_pos_x, win_pos_y + m_window_height),
		true)) {
		button_text = m_is_dark ? ImGui::PreferencesHoverDarkButton : ImGui::PreferencesHoverButton;
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
		ImGui::PushStyleColor(ImGuiCol_Border, { 0,0,0,0 });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8 * scale, 1 * scale });
		ImGui::BeginTooltip();
		imgui.text(_u8L("Open Preferences"));
		ImGui::EndTooltip();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}

	ImVec2 button_pic_size = ImGui::CalcTextSize(into_u8(button_text).c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(m_window_width - m_line_height * 1.75f);
	ImGui::SetCursorPosY(m_window_height - button_size.y - m_close_b_w / 4.f);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		wxGetApp().open_preferences();
	}

	ImGui::PopStyleColor(5);
	m_minimize_b_visible = true;
}
void NotificationManager::HintNotification::render_right_arrow_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	auto scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::wstring button_text;
	button_text = m_is_dark ? ImGui::RightArrowDarkButton : ImGui::RightArrowButton;

	ImVec2 button_pic_size = ImGui::CalcTextSize(into_u8(button_text).c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos_x - m_window_width / 7.5f, win_pos_y + m_window_height - 1.5f * m_line_height),
		ImVec2(win_pos_x - m_window_width / 15.f, win_pos_y + m_window_height),
		true))
	{
		button_text = m_is_dark ? ImGui::RightArrowHoverDarkButton : ImGui::RightArrowHoverButton;
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
		ImGui::PushStyleColor(ImGuiCol_Border, { 0,0,0,0 });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8 * scale, 1 * scale });
		ImGui::BeginTooltip();
		imgui.text(_u8L("Open next tip"));
		ImGui::EndTooltip();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}
	ImGui::SetCursorPosX(m_window_width - m_line_height * 3.f);
	ImGui::SetCursorPosY(m_window_height - button_size.y - m_close_b_w / 4.f);
	if (imgui.button(button_text.c_str(), button_size.x * 0.8f, button_size.y * 1.f))
	{
		retrieve_data();
	}

	ImGui::PopStyleColor(5);
}
void NotificationManager::HintNotification::render_logo(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
}
void NotificationManager::HintNotification::render_documentation_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	auto scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::wstring button_text;
	button_text = m_is_dark ? ImGui::DocumentationDarkButton : ImGui::DocumentationButton;
	std::string placeholder_text;
	placeholder_text = ImGui::EjectButton;

	ImVec2 button_pic_size = ImGui::CalcTextSize(placeholder_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - m_line_height * 5.f, win_pos.y + win_size.y / 2 - button_pic_size.y),
		ImVec2(win_pos.x - m_line_height * 2.5f, win_pos.y + win_size.y / 2 + button_pic_size.y),
		true))
	{
		button_text = m_is_dark ? ImGui::DocumentationHoverDarkButton : ImGui::DocumentationHoverButton;
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
		ImGui::PushStyleColor(ImGuiCol_Border, { 0,0,0,0 });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8 * scale, 1 * scale });
		ImGui::BeginTooltip();
		imgui.text(_u8L("Open documentation in web browser"));
		ImGui::EndTooltip();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			open_documentation();
	}
	ImGui::SetCursorPosX(win_size.x - m_line_height * 5.0f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		open_documentation();
	}

	ImGui::PopStyleColor(5);
}

void NotificationManager::HintNotification::open_documentation()
{
	if (!m_documentation_link.empty())
	{
		launch_browser_if_allowed(m_documentation_link);
	}
}
void NotificationManager::HintNotification::retrieve_data(bool new_hint )
{
	HintData* hint_data = HintDatabase::get_instance().get_hint(new_hint ? HintDataNavigation::Next : HintDataNavigation::Curr);
	if (hint_data == nullptr)
		close();

	if (hint_data != nullptr)
	{
		NotificationData nd{ NotificationType::DidYouKnowHint,
								NotificationLevel::HintNotificationLevel,
								0,
								hint_data->text,
								hint_data->hypertext, nullptr,
								hint_data->follow_text };
		m_hypertext_callback = hint_data->callback;
		m_disabled_tags = hint_data->disabled_tags;
		m_enabled_tags = hint_data->enabled_tags;
		m_runtime_disable = hint_data->runtime_disable;
		m_documentation_link = hint_data->documentation_link;
		m_has_hint_data = true;
		update(nd);
	}
}
}
}
