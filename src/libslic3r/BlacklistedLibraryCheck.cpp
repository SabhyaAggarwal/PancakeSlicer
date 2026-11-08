#include "BlacklistedLibraryCheck.hpp"

#include <cstdio>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/convert.hpp>
#include <algorithm>
#include <string>
#include <vector>

#ifdef  WIN32
#include <psapi.h>
# endif

namespace Slic3r {

#ifdef  WIN32

const std::vector<std::wstring> BlacklistedLibraryCheck::blacklist({ L"NahimicOSD.dll", L"SS2OSD.dll", L"amhook.dll", L"AMHook.dll" });

bool BlacklistedLibraryCheck::get_blacklisted(std::vector<std::wstring>& names)
{
    if (m_found.empty())
        return false;
    for (const auto& lib : m_found)
        names.emplace_back(lib);
    return true;
}

std::wstring BlacklistedLibraryCheck::get_blacklisted_string()
{
    std::wstring ret;
    for (const auto& lib : m_found)
        ret += lib + L"\n";
    return ret;
}

bool BlacklistedLibraryCheck::perform_check()
{   
    HANDLE  hCurrentProcess = GetCurrentProcess();

    HMODULE hMods[1024];
    DWORD   cbNeeded;
    if (EnumProcessModulesEx(hCurrentProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
    {
        for (unsigned int i = 0; i < cbNeeded / sizeof(HMODULE); ++ i)
        {
            wchar_t szModName[MAX_PATH];
            if (GetModuleFileNameExW(hCurrentProcess, hMods[i], szModName, MAX_PATH))
            {
                if (BlacklistedLibraryCheck::is_blacklisted(szModName)) {
                    if (std::find(m_found.begin(), m_found.end(), szModName) == m_found.end())
                        m_found.emplace_back(szModName);
                } 
            }
        }
    }

    return !m_found.empty();
}

bool BlacklistedLibraryCheck::is_blacklisted(const std::wstring &dllpath)
{
    std::wstring dllname = boost::filesystem::path(dllpath).filename().wstring();
    if (std::find(BlacklistedLibraryCheck::blacklist.begin(), BlacklistedLibraryCheck::blacklist.end(), dllname) != BlacklistedLibraryCheck::blacklist.end()) {
        return true;
    }
    return false;
}
bool BlacklistedLibraryCheck::is_blacklisted(const std::string &dllpath)
{
    return BlacklistedLibraryCheck::is_blacklisted(boost::nowide::widen(dllpath));
}

#endif

}
