/* Stub: redirect to XKBlib.h which is installed on this system via libX11-devel.
 * CMake's FindX11 looks for X11/Xkb.h but GLFW actually only includes
 * X11/XKBlib.h, so this redirect is safe. */
#pragma once
#include <X11/XKBlib.h>
