#ifndef slic3r_PyPluginPackage_hpp_
#define slic3r_PyPluginPackage_hpp_

#include <pybind11/pybind11.h>

namespace Slic3r {

class PyPluginPackage
{
public:
    virtual ~PyPluginPackage() = default;
    virtual void register_capabilities() {}
};

class PyPluginPackageTrampoline : public PyPluginPackage
{
public:
    using PyPluginPackage::PyPluginPackage;

    void register_capabilities() override { PYBIND11_OVERRIDE(void, PyPluginPackage, register_capabilities); }
};

}

#endif
