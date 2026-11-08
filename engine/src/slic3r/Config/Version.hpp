#ifndef slic3r_GUI_ConfigIndex_
#define slic3r_GUI_ConfigIndex_

#include <cstddef>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "libslic3r/Semver.hpp"

namespace Slic3r { 
namespace GUI {
namespace Config {

struct Version
{
	Semver 		config_version     = Semver::invalid();
	Semver 		min_slic3r_version = Semver::zero();
	Semver 		max_slic3r_version = Semver::inf();
	std::string comment;

	bool 		is_slic3r_supported(const Semver &slicer_version) const;
	bool 		is_current_slic3r_supported() const;
	bool 		is_current_slic3r_downgrade() const;
};

class Index
{
public:
    typedef std::vector<Version>::const_iterator const_iterator;
	size_t						load(const boost::filesystem::path &path);

	const std::string&			vendor() const { return m_vendor; }
	Semver						version() const;

	const_iterator				begin()   const { return m_configs.begin(); }
	const_iterator				end()     const { return m_configs.end(); }
	const_iterator 				find(const Semver &ver) const;
	const std::vector<Version>& configs() const { return m_configs; }
	const_iterator				recommended() const;
	const_iterator				recommended(const Semver &slic3r_version) const;

	const boost::filesystem::path& path() const { return m_path; }

	static std::vector<Index>	load_db();

private:
	std::string 				m_vendor;
	std::vector<Version>		m_configs;
	boost::filesystem::path		m_path;
};

}
}
}

#endif
