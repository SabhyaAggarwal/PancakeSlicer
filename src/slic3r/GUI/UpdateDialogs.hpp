#ifndef slic3r_UpdateDialogs_hpp_
#define slic3r_UpdateDialogs_hpp_

#include "slic3r/GUI/GUI_Utils.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wx/gdicmn.h>
#include <wx/hyperlink.h>
#include <wx/string.h>

#include "libslic3r/Semver.hpp"
#include "MsgDialog.hpp"

class wxBoxSizer;
class wxCheckBox;

namespace Slic3r {

namespace GUI {


class MsgUpdateSlic3r : public MsgDialog
{
public:
	MsgUpdateSlic3r(const Semver &ver_current, const Semver &ver_online);
	MsgUpdateSlic3r(MsgUpdateSlic3r &&) = delete;
	MsgUpdateSlic3r(const MsgUpdateSlic3r &) = delete;
	MsgUpdateSlic3r &operator=(MsgUpdateSlic3r &&) = delete;
	MsgUpdateSlic3r &operator=(const MsgUpdateSlic3r &) = delete;
	virtual ~MsgUpdateSlic3r();

	bool disable_version_check() const;

	void on_hyperlink(wxHyperlinkEvent& evt);
private:
};


class MsgUpdateConfig : public DPIDialog
{
public:
	struct Update
	{
		std::string vendor;
		Semver version;
		std::string comment;
		std::string change_log;

		Update(std::string vendor, Semver version, std::string comment, std::string changelog)
			: vendor(std::move(vendor))
			, version(std::move(version))
			, comment(std::move(comment))
			, change_log(std::move(changelog))
		{}
	};

    MsgUpdateConfig(const std::vector<Update> &updates, bool force_before_wizard = false);
    void on_dpi_changed(const wxRect &suggested_rect);
	~MsgUpdateConfig();
};

class MsgUpdateForced : public MsgDialog
{
public:
	struct Update
	{
		std::string vendor;
		Semver version;
		std::string comment;
		std::string change_log;

		Update(std::string vendor, Semver version, std::string comment, std::string changelog)
			: vendor(std::move(vendor))
			, version(std::move(version))
			, comment(std::move(comment))
			, change_log(std::move(changelog))
		{}
	};

	MsgUpdateForced(const std::vector<Update>& updates);
	MsgUpdateForced(MsgUpdateForced&&) = delete;
	MsgUpdateForced(const MsgUpdateForced&) = delete;
	MsgUpdateForced& operator=(MsgUpdateForced&&) = delete;
	MsgUpdateForced& operator=(const MsgUpdateForced&) = delete;
	~MsgUpdateForced();
};

class MsgDataIncompatible : public MsgDialog
{
public:
	MsgDataIncompatible(const std::unordered_map<std::string, wxString> &incompats);
	MsgDataIncompatible(MsgDataIncompatible &&) = delete;
	MsgDataIncompatible(const MsgDataIncompatible &) = delete;
	MsgDataIncompatible &operator=(MsgDataIncompatible &&) = delete;
	MsgDataIncompatible &operator=(const MsgDataIncompatible &) = delete;
	~MsgDataIncompatible();
};

class MsgNoUpdates : public MsgDialog
{
public:
	MsgNoUpdates();
	MsgNoUpdates(MsgNoUpdates&&) = delete;
	MsgNoUpdates(const MsgNoUpdates&) = delete;
	MsgNoUpdates& operator=(MsgNoUpdates&&) = delete;
	MsgNoUpdates& operator=(const MsgNoUpdates&) = delete;
	~MsgNoUpdates();
};

}
}

#endif
