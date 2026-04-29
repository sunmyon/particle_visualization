/* Minimal Xinerama stub for building GLFW on systems with the runtime library
 * (libXinerama1) installed but without the -devel headers.
 * All functions are loaded at runtime via dlopen/dlsym by GLFW. */
#pragma once
#include <X11/Xlib.h>
#include <X11/Xfuncproto.h>

typedef struct {
    int   screen_number;
    short x_org;
    short y_org;
    short width;
    short height;
} XineramaScreenInfo;

_XFUNCPROTOBEGIN
extern Bool             XineramaQueryExtension(Display *dpy, int *event_base, int *error_base);
extern Status           XineramaQueryVersion(Display *dpy, int *major, int *minor);
extern Bool             XineramaIsActive(Display *dpy);
extern XineramaScreenInfo *XineramaQueryScreens(Display *dpy, int *number);
_XFUNCPROTOEND
