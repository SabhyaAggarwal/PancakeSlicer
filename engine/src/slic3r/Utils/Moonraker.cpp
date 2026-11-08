#include "Moonraker.hpp"

#include <exception>
#include <sstream>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <wx/string.h>
#include <utility>
#include <wx/arrstr.h>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {

Moonraker::Moonraker(DynamicPrintConfig *config)
    : m_host(config->opt_string("print_host"))
    , m_apikey(config->opt_string("printhost_apikey"))
    , m_cafile(config->opt_string("printhost_cafile"))
    , m_ssl_revoke_best_effort(config->opt_bool("printhost_ssl_ignore_revoke"))
{}

const char* Moonraker::get_name() const { return "Moonraker"; }

wxString Moonraker::get_test_ok_msg() const
{
    return _(L("Connection to Moonraker is working correctly."));
}

wxString Moonraker::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to Moonraker"), msg);
}

std::string Moonraker::make_url(const std::string &path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/')
            return (boost::format("%1%%2%") % m_host % path).str();
        return (boost::format("%1%/%2%") % m_host % path).str();
    }
    return (boost::format("http://%1%/%2%") % m_host % path).str();
}

void Moonraker::set_auth(Http &http) const
{
    if (!m_apikey.empty())
        http.header("X-Api-Key", m_apikey);
    if (!m_cafile.empty())
        http.ca_file(m_cafile);
}

bool Moonraker::test(wxString &msg) const
{
    const char *name = get_name();
    bool res = true;
    auto url = make_url("server/info");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get server info at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting server info: %2%, HTTP %3%, body: `%4%`")
            % name % error % status % body;
        res = false;
        msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/info body: %2%") % name % body;
        try {
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);

            const auto klippy_state = ptree.get_optional<std::string>("result.klippy_state");
            if (!klippy_state) {
                res = false;
                msg = _L("The host responded but it doesn't look like Moonraker (missing result.klippy_state).");
                return;
            }
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: klippy_state = %2%") % name % (*klippy_state);
        } catch (const std::exception &ex) {
            res = false;
            msg = GUI::format_wxstr(_L("Could not parse Moonraker server response: %s"), ex.what());
        }
    })
#ifdef WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
    .perform_sync();

    return res;
}

bool Moonraker::get_storage(wxArrayString &storage_path, wxArrayString &storage_name) const
{
    const char *name = get_name();
    bool got_any = false;
    auto url = make_url("server/files/roots");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Enumerating storage roots at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        if (status == 404 || status == 501) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/files/roots not implemented (HTTP %2%); upload() will fall back to the \"gcodes\" root.")
                % name % status;
        } else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("%1%: Could not enumerate roots: %2%, HTTP %3%, body: `%4%`")
                % name % error % status % body;
        }
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: /server/files/roots body: %2%") % name % body;
        try {
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);
            const auto result_node = ptree.get_child_optional("result");
            if (!result_node)
                return;
            for (const auto &child : *result_node) {
                const std::string &root = child.second.get<std::string>("name", "");
                const std::string &perms = child.second.get<std::string>("permissions", "");
                if (root.empty() || perms.find('w') == std::string::npos)
                    continue;
                storage_path.Add(wxString::FromUTF8(root));
                storage_name.Add(wxString::FromUTF8(root));
                got_any = true;
            }
        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("%1%: Could not parse roots: %2%") % name % ex.what();
        }
    })
#ifdef WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
    .perform_sync();

    return got_any;
}

bool Moonraker::start_print(wxString &error_msg, const std::string &filename) const
{
    const char *name = get_name();
    bool res = true;
    auto url = make_url("printer/print/start");
    pt::ptree body_tree;
    body_tree.put("filename", filename);
    std::ostringstream body_ss;
    pt::write_json(body_ss, body_tree, false);
    std::string body = body_ss.str();

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Starting print of %2% at %3%") % name % filename % url;

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.header("Content-Type", "application/json")
        .set_post_body(body)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: print/start HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error starting print at %2%: %3%, HTTP %4%, body: `%5%`")
                % name % url % error % status % body;
            res = false;
            error_msg = format_error(body, error, status);
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool Moonraker::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    wxString test_msg;
    if (!test(test_msg)) {
        error_fn(std::move(test_msg));
        return false;
    }

    const char *name = get_name();
    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();
    const std::string root = upload_data.storage.empty() ? std::string("gcodes") : upload_data.storage;

    const bool want_start = upload_data.post_action == PrintHostPostUploadAction::StartPrint;

    std::string url = make_url("server/files/upload");
    bool result = true;
    std::string uploaded_path;
    bool moonraker_started_print = false;

    const std::string plateindex = upload_data.extended("plateindex");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% to %3% (root=%4%, filename=%5%, plateindex=%6%, start_print=%7%)")
        % name
        % upload_data.source_path
        % url
        % root
        % upload_filename.string()
        % (plateindex.empty() ? "-" : plateindex)
        % (want_start ? "true" : "false");

    auto http = Http::post(std::move(url));
    set_auth(http);
    http.form_add("root", root);
    if (!plateindex.empty())
        http.form_add("plateindex", plateindex);
    http.form_add("print", want_start ? "true" : "false");
    http.form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: upload HTTP %2%: %3%") % name % status % body;
            try {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                const auto stored_path = ptree.get_optional<std::string>("item.path");
                if (stored_path) {
                    uploaded_path = *stored_path;
                } else {
                    uploaded_path = upload_filename.string();
                    BOOST_LOG_TRIVIAL(warning) << boost::format(
                        "%1%: upload response missing item.path, falling back to original filename `%2%`")
                        % name % uploaded_path;
                }
                moonraker_started_print = ptree.get<bool>("print_started", false) ||
                                          ptree.get<bool>("print_queued", false);
            } catch (const std::exception &ex) {
                BOOST_LOG_TRIVIAL(warning) << boost::format(
                    "%1%: could not parse upload response (%2%); falling back to original filename")
                    % name % ex.what();
                uploaded_path = upload_filename.string();
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading to %2%: %3%, HTTP %4%, body: `%5%`")
                % name % url % error % status % body;
            error_fn(format_error(body, error, status));
            result = false;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                BOOST_LOG_TRIVIAL(info) << name << ": Upload canceled";
                result = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    if (!result)
        return false;

    if (want_start && !uploaded_path.empty() && !moonraker_started_print) {
        wxString start_msg;
        if (!start_print(start_msg, uploaded_path)) {
            error_fn(std::move(start_msg));
            return false;
        }
    }
    return true;
}

}
