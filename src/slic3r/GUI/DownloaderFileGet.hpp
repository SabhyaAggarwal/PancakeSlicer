#ifndef slic3r_DownloaderFileGet_hpp_
#define slic3r_DownloaderFileGet_hpp_


#include <boost/filesystem/path.hpp>
#include <memory>
#include <string>
#include <wx/event.h>
#include <wx/frame.h>
#include <boost/filesystem.hpp>

namespace Slic3r {
namespace GUI {
class FileGet : public std::enable_shared_from_this<FileGet> {
private:
	struct priv;
public:
	FileGet(int ID, std::string url, const std::string& filename, wxEvtHandler* evt_handler,const boost::filesystem::path& dest_folder);
	FileGet(FileGet&& other);
	~FileGet();

	void get();
	void cancel();
	void pause();
	void resume();
	static std::string	escape_url(const std::string& url);
	static bool			is_subdomain(const std::string& url, const std::string& domain);
private:
	std::unique_ptr<priv> p;
};
wxDECLARE_EVENT(EVT_DWNLDR_FILE_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_DWNLDR_FILE_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_DWNLDR_FILE_ERROR, wxCommandEvent);
wxDECLARE_EVENT(EVT_DWNLDR_FILE_NAME_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_DWNLDR_FILE_PAUSED, wxCommandEvent);
wxDECLARE_EVENT(EVT_DWNLDR_FILE_CANCELED, wxCommandEvent);
}
}
#endif
