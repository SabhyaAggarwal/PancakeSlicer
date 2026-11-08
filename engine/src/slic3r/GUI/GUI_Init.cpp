#include "GUI_Init.hpp"

#include "libslic3r/AppConfig.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/InstanceCheck.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <vector>
#include <utility>
#include <wx/init.h>
#include "libslic3r/Exception.hpp"
#include <boost/log/trivial.hpp>
#include <ostream>
#include <exception>
#include <wx/msgdlg.h>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/convert.hpp>
#include <wx/string.h>

#if __APPLE__
    #include <signal.h>
#endif

namespace Slic3r {
namespace GUI {

const std::vector<std::pair<int, int>> OpenGLVersions::core    = { {3,2}, {3,3}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6} };

int GUI_Run(GUI_InitParams &params)
{
#if __APPLE__
    signal(SIGCHLD, SIG_DFL);
#endif
    try {
        GUI::GUI_App* gui = new GUI::GUI_App();
            bool gui_single_instance_setting = gui->app_config->get("app", "single_instance") == "true";
            if (Slic3r::instance_check(params.argc, params.argv, gui_single_instance_setting)) {
                return -1;
            }
        GUI::GUI_App::SetInstance(gui);
        gui->init_params = &params;

        if (params.argc > 1) {
            int                 argc = 1;
            std::vector<char *> argv;
            argv.push_back(params.argv[0]);
            return wxEntry(argc, argv.data());
        } else {
            return wxEntry(params.argc, params.argv);
        }
    } catch (const Slic3r::Exception &ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what() << std::endl;
        wxMessageBox(boost::nowide::widen(ex.what()), _L("Orca Slicer GUI initialization failed"), wxICON_STOP);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what() << std::endl;
        wxMessageBox(format_wxstr(_L("Fatal error, exception: %1%"), ex.what()), _L("Orca Slicer GUI initialization failed"), wxICON_STOP);
    }
    return 1;
}
}
}
