#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/event.h>

namespace Slic3r {


class AppConfig;
class PresetBundle;
class Semver;

static constexpr const int SLIC3R_VERSION_BODY_MAX = 256;

class PresetUpdater
{
public:
	PresetUpdater();
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	void sync(std::string http_url, std::string language, std::string plugin_version, PresetBundle *preset_bundle);

	void slic3r_update_notify();

	enum UpdateResult {
		R_NOOP,
		R_INCOMPAT_EXIT,
		R_INCOMPAT_CONFIGURED,
		R_UPDATE_INSTALLED,
		R_UPDATE_REJECT,
		R_UPDATE_NOTIFICATION,
		R_ALL_CANCELED
	};

	enum class UpdateParams {
		SHOW_TEXT_BOX,
		SHOW_NOTIFICATION,
		FORCED_BEFORE_WIZARD
	};

	UpdateResult config_update(const Semver &old_slic3r_version, UpdateParams params) const;

	bool install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot = true) const;

	void on_update_notification_confirm();
    void do_printer_config_update();
	void check_vendor_update(const std::string& vendor_id);
	void check_new_vendors(const std::set<std::string>& system_vendors,
	                        std::function<void(std::vector<std::string> installed_vendors, bool declined)> callback);

	bool version_check_enabled() const;

private:
	struct priv;
	std::unique_ptr<priv> p;
};

wxDECLARE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);


}
#endif
