#import "RetinaHelper.hpp"
#import "RetinaHelperImpl.hmm"
#import <OpenGL/OpenGL.h>

#import "wx/window.h"

@implementation RetinaHelperImpl

namespace Slic3r {
namespace GUI {

RetinaHelper::RetinaHelper(wxWindow *window)
{
    m_self = nullptr;
    m_self = [[RetinaHelperImpl alloc] initWithView:window->GetHandle() handler:window->GetEventHandler()];
}

RetinaHelper::~RetinaHelper()
{
    [(id)m_self release];
}

void RetinaHelper::set_use_retina(bool aValue)
{
    [(id)m_self setViewWantsBestResolutionOpenGLSurface:aValue];
}

bool RetinaHelper::get_use_retina()
{
    return [(id)m_self getViewWantsBestResolutionOpenGLSurface];
}

float RetinaHelper::get_scale_factor()
{
    return [(id)m_self getViewWantsBestResolutionOpenGLSurface] ? [(id)m_self getBackingScaleFactor] : 1.0f;
}

}
}


-(id)initWithView:(NSView *)aView handler:(wxEvtHandler *)aHandler
{
    self = [super init];
    if (self) {
        handler = aHandler;
        view = aView;
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        if (nc) {
            [nc addObserver:self selector:@selector(windowDidChangeBackingProperties:)
                name:NSWindowDidChangeBackingPropertiesNotification object:nil];
        }

        NSWindow* window = [aView window];
        if (window) {
            [window setColorSpace:[NSColorSpace sRGBColorSpace]];
        }
    }
    return self;
}

-(void) dealloc
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    if (nc) {
        [nc removeObserver:self];
    }
    [super dealloc];
}

-(void)setViewWantsBestResolutionOpenGLSurface:(BOOL)value
{
    [view setWantsBestResolutionOpenGLSurface:value];
}

-(BOOL)getViewWantsBestResolutionOpenGLSurface
{
    return [view wantsBestResolutionOpenGLSurface];
}

-(float)getBackingScaleFactor
{
    return [[view window] backingScaleFactor];
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    NSWindow *theWindow = (NSWindow *)[notification object];

    if (theWindow == [view window]) {
        CGFloat newBackingScaleFactor = [theWindow backingScaleFactor];
        CGFloat oldBackingScaleFactor = [[[notification userInfo]
            objectForKey:@"NSBackingPropertyOldScaleFactorKey"]
            doubleValue];

        if (newBackingScaleFactor != oldBackingScaleFactor) {
            wxSizeEvent *event = new wxSizeEvent();
            NSRect nsrect = [view bounds];
            wxRect rect = wxRect(nsrect.origin.x, nsrect.origin.y, nsrect.size.width, nsrect.size.height);
            event->SetRect(rect);
            event->SetSize(rect.GetSize());
            handler->QueueEvent(event);
        }
    }
}
@end
