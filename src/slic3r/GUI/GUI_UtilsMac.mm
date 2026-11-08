#include <unistd.h>
#include <sys/sysctl.h>
#import <Cocoa/Cocoa.h>
#import <wx/osx/cocoa/dataview.h>
#import "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

void dataview_remove_insets(wxDataViewCtrl* dv) {
    NSScrollView* scrollview = (NSScrollView*) ((wxCocoaDataViewControl*)dv->GetDataViewPeer())->GetWXWidget();
    NSOutlineView* outlineview = scrollview.documentView;
    [outlineview setIntercellSpacing: NSMakeSize(0.0, 1.0)];
    if (@available(macOS 11, *)) {
        [outlineview setStyle:NSTableViewStylePlain];
    }
}

void staticbox_remove_margin(wxStaticBox* sb) {
    NSBox* nativeBox = (NSBox*)sb->GetHandle();
    [nativeBox setBoxType:NSBoxCustom];
    [nativeBox setBorderWidth:0];
}

void set_window_corner_radius(wxWindow* win, int radius) {
    if (!win)
        return;
    NSView* view = (NSView*)win->GetHandle();
    if (!view)
        return;
    NSWindow* window = [view window];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [view setWantsLayer:YES];
    [[view layer] setCornerRadius:radius];
    [[view layer] setMasksToBounds:YES];
}

bool is_debugger_present()
{
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

    info.kp_proc.p_flag = 0;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
}

}
}
