#ifndef __Http_hpp__
#define __Http_hpp__

#include <cstddef>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r_version.h"

#define MAX_SIZE_TO_FILE    3*1024

namespace Slic3r {

enum HttpErrorCode
{
	HttpErrorResourcesNotFound	= 2,
	HtttErrorNoDevice			= 3,
	HttpErrorRequestLogin		= 4,
	HttpErrorResourcesNotExists = 6,
	HttpErrorMQTTError			= 7,
	HttpErrorResourcesForbidden = 8,
	HttpErrorInternalRequestError = 9,
	HttpErrorInternalError		= 10,
	HttpErrorFileFormatError	= 11,
	HttpErrorResoucesConflict	= 12,
	HttpErrorTimeout			= 13,
	HttpErrorResourcesExhaust   = 14,
	HttpErrorVersionLimited		= 15,
};

class Http : public std::enable_shared_from_this<Http> {
private:
	struct priv;
public:
	struct Progress
	{
		size_t dltotal;
		size_t dlnow;
		size_t ultotal;
		size_t ulnow;
        const std::string& buffer;
        double upload_spd{0.0f};

		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow, const std::string& buffer) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow), buffer(buffer)
		{}

		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow, const std::string& buffer, double ulspd) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow), buffer(buffer), upload_spd(ulspd)
		{}
	};

	typedef std::shared_ptr<Http> Ptr;
	typedef std::function<void(std::string , unsigned )> CompleteFn;

	typedef std::function<void(std::string , std::string , unsigned )> ErrorFn;

	typedef std::function<void(Progress, bool& )> ProgressFn;

	typedef std::function<void(std::string )> IPResolveFn;

	typedef std::function<void(std::string headers)> HeaderCallbackFn;

	Http(Http &&other);

	static Http get(std::string url);
	static Http post(std::string url);
	static Http put(std::string url);
	static Http del(std::string url);

	static Http put2(std::string url);
	static Http patch(std::string url);

	static void set_extra_headers(std::map<std::string, std::string> headers);
	static std::map<std::string, std::string> get_extra_headers();

	~Http();

	Http(const Http &) = delete;
	Http& operator=(const Http &) = delete;
	Http& operator=(Http &&) = delete;

	Http& timeout_connect(long timeout);
    Http& timeout_max(long timeout);
	Http& size_limit(size_t sizeLimit);
	Http& set_range(const std::string& range);
	Http& header(std::string name, const std::string &value);
	Http& headers_reset();
	Http& remove_header(std::string name);
	Http& auth_digest(const std::string &user, const std::string &password);
    Http& auth_basic(const std::string &user, const std::string &password);
	Http& ca_file(const std::string &filename);
	Http& tls_verify(bool enable);

	Http& form_clear();
	Http& form_add(const std::string &name, const std::string &contents);
	Http& form_add_file(const std::string &name, const boost::filesystem::path &path, boost::filesystem::ifstream::off_type offset = 0, size_t length = 0);
	Http& mime_form_add_text(std::string& name, std::string& value);
	Http& mime_form_add_file(std::string& name, const char* path);
    Http& form_add_file(const std::wstring& name, const boost::filesystem::path& path, boost::filesystem::ifstream::off_type offset = 0, size_t length = 0);
	Http& form_add_file(const std::string &name, const boost::filesystem::path &path, const std::string &filename, boost::filesystem::ifstream::off_type offset = 0, size_t length = 0);

#ifdef WIN32
	Http& ssl_revoke_best_effort(bool set);
#endif
	Http& set_post_body(const boost::filesystem::path &path);

	Http& set_post_body(const std::string &body);

	Http& set_put_body(const boost::filesystem::path &path);

	Http& set_del_body(const std::string& body);

	Http& on_complete(CompleteFn fn);
	Http& on_error(ErrorFn fn);
	Http& on_progress(ProgressFn fn);
	Http& on_ip_resolve(IPResolveFn fn);
	Http& on_header_callback(HeaderCallbackFn fn);

	Ptr perform();
	void perform_sync();
	void cancel();

	void print() const;

	static bool ca_file_supported();

    static std::string tls_global_init();
    static std::string tls_system_cert_store();

	static std::string url_encode(const std::string &str);
	static std::string url_decode(const std::string &str);

	static std::string get_filename_from_url(const std::string &url);
	static std::string get_host_from_url(const std::string &url, std::string *port = nullptr);
	static std::string get_host_header_value(const std::string &url);
private:
	Http(const std::string &url);

	std::unique_ptr<priv> p;
};

std::ostream& operator<<(std::ostream &, const Http::Progress &);


}

#endif
