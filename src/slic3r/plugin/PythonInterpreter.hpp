#ifndef slic3r_PythonInterpreter_hpp_
#define slic3r_PythonInterpreter_hpp_

#include <cstddef>
#include <pybind11/conduit/wrap_include_python_h.h>
#include <pytypedefs.h>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pybind11 {
class scoped_interpreter;
class error_already_set;
}

namespace Slic3r {

class PythonRuntimeLease;

void log_python_exception_keep(pybind11::error_already_set& err);


class PythonInterpreter
{
public:
    static PythonInterpreter& instance();

    bool initialize();

    bool is_initialized() const { return m_initialized.load(std::memory_order_acquire); }

    PythonRuntimeLease acquire_runtime_lease();

    const std::string& last_error() const { return m_last_error; }

    static std::string shared_packages_dir();

    static std::string bundled_python_executable();

    static std::string bundled_uv_path();

    static std::string python_abi_tag();

    void shutdown();

    bool add_plugin_sys_path(const std::string& path, std::string& error);

    PyObject* load_module_from_file(const std::string& file_path,
                                    std::string&       error,
                                    std::vector<std::string>* plugin_paths = nullptr,
                                    std::vector<std::string>* plugin_modules = nullptr);
    PyObject* load_module_from_whl(const std::string& whl_path,
                                   const std::string& pkg_name,
                                   std::string&       error,
                                   std::vector<std::string>* plugin_paths = nullptr,
                                   std::vector<std::string>* plugin_modules = nullptr);
    PyObject* load_module_from_directory(const std::string& dir_path,
                                         const std::string& pkg_name,
                                         std::string&       error,
                                         std::vector<std::string>* plugin_paths = nullptr,
                                         std::vector<std::string>* plugin_modules = nullptr);

    void unload_module(PyObject* module,
                       const std::string& module_name,
                       const std::vector<std::string>& plugin_paths,
                       const std::vector<std::string>& plugin_modules);

    ~PythonInterpreter();

private:
    friend class PythonRuntimeLease;

    PythonInterpreter() = default;
    PythonInterpreter(const PythonInterpreter&) = delete;
    PythonInterpreter& operator=(const PythonInterpreter&) = delete;

    bool add_plugin_sys_path_locked(const std::string& path, std::string& error);
    void remove_plugin_sys_paths_locked(const std::vector<std::string>& paths);
    void record_plugin_modules_locked(const std::string& module_name,
                                      const std::vector<std::string>& plugin_paths,
                                      std::vector<std::string>* plugin_modules);
    void remove_plugin_modules_locked(const std::vector<std::string>& plugin_modules);
    void remove_module_tree_locked(const std::string& module_name);

    std::atomic<bool> m_initialized{false};
    mutable std::shared_mutex m_runtime_mutex;
    PyThreadState* m_main_thread_state = nullptr;
    std::unique_ptr<pybind11::scoped_interpreter> m_interpreter;
    std::string m_last_error;
    std::unordered_map<std::string, std::size_t> m_plugin_path_users;
    std::unordered_map<std::string, bool>        m_plugin_path_owned;
    std::unordered_map<std::string, std::size_t> m_plugin_module_users;
    std::unordered_map<std::string, bool>        m_plugin_module_owned;
};

class PythonRuntimeLease
{
public:
    PythonRuntimeLease() = default;
    PythonRuntimeLease(PythonRuntimeLease&& other) noexcept;
    PythonRuntimeLease& operator=(PythonRuntimeLease&& other) noexcept;
    ~PythonRuntimeLease();

    PythonRuntimeLease(const PythonRuntimeLease&)            = delete;
    PythonRuntimeLease& operator=(const PythonRuntimeLease&) = delete;

    explicit operator bool() const { return m_interpreter != nullptr; }

private:
    friend class PythonInterpreter;

    explicit PythonRuntimeLease(PythonInterpreter& interpreter);
    void release();

    static thread_local PythonInterpreter* s_owner;
    static thread_local unsigned int      s_depth;

    PythonInterpreter*                  m_interpreter = nullptr;
    std::shared_lock<std::shared_mutex> m_lock;
};

inline PythonRuntimeLease PythonInterpreter::acquire_runtime_lease()
{
    return PythonRuntimeLease(*this);
}

class PythonGILState
{
public:
    PythonGILState() {
        m_runtime_lease = PythonInterpreter::instance().acquire_runtime_lease();
        if (m_runtime_lease) {
            m_state    = PyGILState_Ensure();
            m_acquired = true;
        }
    }

    ~PythonGILState() {
        if (m_acquired)
            PyGILState_Release(m_state);
    }

    explicit operator bool() const { return m_acquired; }

private:
    PythonRuntimeLease m_runtime_lease;
    PyGILState_STATE   m_state{};
    bool               m_acquired = false;
};

class PyObjectPtr
{
public:
    explicit PyObjectPtr(PyObject* obj = nullptr) : m_obj(obj) {}

    ~PyObjectPtr() {
        if (m_obj) {
            Py_DECREF(m_obj);
        }
    }

    PyObject* get() const { return m_obj; }
    PyObject* release() {
        PyObject* temp = m_obj;
        m_obj = nullptr;
        return temp;
    }

    void reset(PyObject* obj = nullptr) {
        if (m_obj) {
            Py_DECREF(m_obj);
        }
        m_obj = obj;
    }

    operator bool() const { return m_obj != nullptr; }

private:
    PyObject* m_obj;

    PyObjectPtr(const PyObjectPtr&) = delete;
    PyObjectPtr& operator=(const PyObjectPtr&) = delete;
};

}

#endif
