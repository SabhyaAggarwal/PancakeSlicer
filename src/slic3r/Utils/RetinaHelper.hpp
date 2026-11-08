#ifndef slic3r_RetinaHelper_hpp_
#define slic3r_RetinaHelper_hpp_

class wxWindow;


namespace Slic3r {
namespace GUI {

class RetinaHelper
{
public:
    RetinaHelper(wxWindow* window);
    ~RetinaHelper();

    void set_use_retina(bool value);
    bool get_use_retina();
    float get_scale_factor();

private:
#ifdef __WXGTK3__
    wxWindow* m_window;
#endif
    void* m_self;
};


}
}

#endif
