/* Minimal XInput2 stub for building GLFW on systems with libXi runtime
 * installed but without the -devel headers.
 * All functions (XISelectEvents, XIQueryVersion) are loaded at runtime via
 * dlopen/dlsym by GLFW.  Only the types and macros used at compile time are
 * defined here. */
#pragma once
#include <X11/Xlib.h>
#include <X11/Xfuncproto.h>

/* XI2 version */
#define XI_2_Major   2
#define XI_2_Minor   0

/* XIAllDevices / XIAllMasterDevices */
#define XIAllDevices        0
#define XIAllMasterDevices  1

/* Raw event types used by GLFW */
#define XI_RawMotion        17
#define XI_LASTEVENT        26

/* Mask helpers */
#define XIMaskLen(event)         (((event) >> 3) + 1)
#define XISetMask(ptr, event)    (((unsigned char*)(ptr))[(event)>>3]  |=  (1 << ((event) & 7)))
#define XIClearMask(ptr, event)  (((unsigned char*)(ptr))[(event)>>3]  &= ~(1 << ((event) & 7)))
#define XIMaskIsSet(ptr, event)  (((unsigned char*)(ptr))[(event)>>3]  &   (1 << ((event) & 7)))

typedef struct {
    int           deviceid;
    int           mask_len;
    unsigned char *mask;
} XIEventMask;

typedef struct {
    int     mask_len;
    unsigned char *mask;
    double *values;
} XIValuatorState;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    int            extension;
    int            evtype;
    Time           time;
    int            deviceid;
    int            sourceid;
    int            detail;
    int            flags;
    XIValuatorState valuators;
    double        *raw_values;
} XIRawEvent;

_XFUNCPROTOBEGIN
extern int    XISelectEvents(Display *dpy, Window win, XIEventMask *masks, int num_masks);
extern Status XIQueryVersion(Display *dpy, int *major_version_inout, int *minor_version_inout);
_XFUNCPROTOEND
