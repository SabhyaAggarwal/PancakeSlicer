#ifndef slic3r_GUI_Snapshot_
#define slic3r_GUI_Snapshot_

#include <ctime>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "libslic3r/Semver.hpp"
#include "Version.hpp"

namespace Slic3r { 

class AppConfig;

namespace GUI {
namespace Config {


class Snapshot
{
public:
	enum Reason {
		SNAPSHOT_UNKNOWN,
		SNAPSHOT_UPGRADE,
		SNAPSHOT_DOWNGRADE,
		SNAPSHOT_BEFORE_ROLLBACK,
		SNAPSHOT_USER,
	};

	Snapshot() { clear(); }

	void 		clear();
	void 		load_ini(const std::string &path);
	void 		save_ini(const std::string &path);

	void 		export_selections(AppConfig &config) const;
	void 		export_vendor_configs(AppConfig &config) const;

	bool 		equal_to_active(const AppConfig &app_config) const;

	std::string					id;
	std::time_t					time_captured;
	Semver		 				slic3r_version_captured = Semver::invalid();
	std::string 				comment;
	Reason						reason;

	std::string 				format_reason() const;

	std::string 				print;
	std::string 				sla_print;
	std::vector<std::string>	filaments;
	std::string 				sla_material;
	std::string					printer;
	std::string 				physical_printer;

	struct VendorConfig {
		std::string name;
		Version version;
		std::map<std::string, std::set<std::string>> models_variants_installed;
	};
	std::vector<VendorConfig> 	vendor_configs;
};

class SnapshotDB
{
public:
	static SnapshotDB&				singleton();

	typedef std::vector<Snapshot>::const_iterator const_iterator;

	size_t 							load_db();
	void 							update_slic3r_versions(std::vector<Index> &index_db);

	const Snapshot&					take_snapshot(const AppConfig &app_config, Snapshot::Reason reason, const std::string &comment = "");
	const Snapshot&					restore_snapshot(const std::string &id, AppConfig &app_config);
	void 							restore_snapshot(const Snapshot &snapshot, AppConfig &app_config);
	bool 							is_on_snapshot(AppConfig &app_config) const;
	const_iterator					snapshot_with_vendor_preset(const std::string &vendor_name, const Semver &config_version);

	const_iterator					begin()     const { return m_snapshots.begin(); }
	const_iterator					end()       const { return m_snapshots.end(); }
	const_iterator 					snapshot(const std::string &id) const;
	const std::vector<Snapshot>& 	snapshots() const { return m_snapshots; }

private:
	static boost::filesystem::path	create_db_dir();

	std::vector<Snapshot>			m_snapshots;
};

const Snapshot* take_config_snapshot_report_error(const AppConfig &app_config, Snapshot::Reason reason, const std::string &comment);

bool take_config_snapshot_cancel_on_error(const AppConfig &app_config, Snapshot::Reason reason, const std::string &comment, const std::string &message);

}
}
}

#endif
