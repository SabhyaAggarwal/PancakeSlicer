#ifndef slic3r_PluginAuditManager_hpp_
#define slic3r_PluginAuditManager_hpp_

#include <boost/filesystem/path.hpp>
#include <pybind11/conduit/wrap_include_python_h.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/filesystem.hpp>

namespace Slic3r {

struct AuditDecision {
    bool        allowed = true;
    std::string reason;
};

struct AuditViolation {
    std::string            plugin_key;
    std::string            event_name;
    std::string            reason;
};

struct AllowedRoot {
    boost::filesystem::path path;
    bool                    allow_write = true;
};

enum class AuditEventCategory {
    None,
    FsRead,
    FsReadWrite,
    FsCreate,
    FsDelete,
    Http,
    Socket,
    ProcessCreate,
    ProcessReplace,
    Threading,
};

bool is_inside_allowed_root(const boost::filesystem::path& candidate,
                            const boost::filesystem::path& allowed_root);

class PluginAuditManager
{
public:
    static PluginAuditManager& instance();

    void install_hook();

    void        set_current_plugin(const std::string& plugin_key);
    std::string current_plugin() const;
    void        clear_current_plugin();

    void        set_current_capability(const std::string& capability_name);
    std::string current_capability() const;
    void        clear_current_capability();

    void add_global_allowed_root(const boost::filesystem::path& root, bool allow_write = true);
    void add_scoped_allowed_root(const boost::filesystem::path& root, bool allow_write = true);

    void add_denied_filename(const std::string& filename);

    static std::vector<std::string> default_denied_filenames();

    bool is_denied_filename(const boost::filesystem::path& candidate) const;

    void add_denied_path_keyword(const std::string& keyword);

    static std::vector<std::string> default_denied_path_keywords();

    bool is_denied_path_keyword(const boost::filesystem::path& candidate) const;

    bool is_denied_path(const boost::filesystem::path& candidate) const;

    AuditDecision check_path_access(const boost::filesystem::path& candidate, bool is_write);
    AuditDecision check_open(const std::string& path, const std::string& mode);

    bool request_filesystem_read_permissions(const std::string& plugin_key,
                                             const std::vector<std::string>& paths);

    void report_violation(const AuditViolation& violation);
    bool audit_denial_pending() const;
    void clear_audit_denial();
    void clear_last_violation();
    bool last_violation(AuditViolation& violation) const;

    bool has_approved_ancestor(const std::string& plugin_key, const std::vector<std::string>& call_site_ids) const;
    void record_approved_call_sites(const std::string& plugin_key, const std::vector<std::string>& call_site_ids);

    bool verbose_events = true;

private:
    friend class ScopedPluginAuditContext;

    PluginAuditManager() = default;

    static int audit_hook(const char* event, PyObject* args, void* user_data);

    static thread_local std::string                   m_current_plugin_key;
    static thread_local std::string                   m_current_capability_name;
    static thread_local std::vector<AllowedRoot>       m_scoped_allowed_roots;
    static thread_local bool                           m_audit_denial_pending;
    static thread_local bool                           m_has_last_violation;
    static thread_local AuditViolation                 m_last_violation;

    mutable std::mutex m_mutex;
    std::vector<AllowedRoot>             m_global_allowed_roots;
    std::vector<std::string>             m_denied_filenames;
    std::vector<std::string>             m_denied_path_keywords;
    std::unordered_map<std::string, std::unordered_set<std::string>> m_approved_call_sites;
};

class ScopedPluginAuditContext
{
public:
    explicit ScopedPluginAuditContext(
        const std::string&                   plugin_key,
        const std::string&                   capability_name = {});

    ~ScopedPluginAuditContext();

    ScopedPluginAuditContext(const ScopedPluginAuditContext&)            = delete;
    ScopedPluginAuditContext& operator=(const ScopedPluginAuditContext&) = delete;

private:
    std::string                   m_previous_id;
    std::string                   m_previous_capability;
    std::vector<AllowedRoot>      m_previous_scoped_roots;
};

}

#endif
