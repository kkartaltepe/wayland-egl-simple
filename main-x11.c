// clang-format off
// Build this with:
// $ gcc -g -o demox11 main-x11.c -lxcb -lX11-xcb -lX11 -lEGL
// clang-format on

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>

// Window system information
struct wsi {
  Display *display;
  xcb_connection_t *connection;
  xcb_window_t window;
  struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
  } egl;

  int32_t w, h;
};

struct wsi WSI = {0};
static bool should_exit = false;
static bool draw_next_frame = false;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT;
PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;

static const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE,
    EGL_WINDOW_BIT,
    EGL_RED_SIZE,
    8,
    EGL_GREEN_SIZE,
    8,
    EGL_BLUE_SIZE,
    8,
    EGL_RENDERABLE_TYPE,
    EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_BIT,
    EGL_NONE,
};

static const EGLint context_attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION,
    2,
    EGL_NONE,
};

EGLConfig configs[256]; // allocate some space for eglGetConfigs.

typedef void(APIENTRYP CLEARCOLOR)(GLclampf red, GLclampf green, GLclampf blue,
                                   GLclampf alpha);
typedef void(APIENTRYP CLEAR)(GLbitfield mask);

int main(int argc, char *argv[]) {
  WSI.display = XOpenDisplay(NULL);
  assert(WSI.display);
  WSI.connection = XGetXCBConnection(WSI.display);
  assert(WSI.connection);
  XSetEventQueueOwner(WSI.display, XCBOwnsEventQueue);

  // initialize EGL for wayland.
  eglCreatePlatformWindowSurfaceEXT =
      (void *)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
  eglGetPlatformDisplayEXT =
      (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
  int egl_major = 0;
  int egl_minor = 0;
  WSI.egl.display =
      eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, WSI.display, NULL);
  assert(WSI.egl.display && WSI.egl.display != EGL_NO_DISPLAY);
  assert(eglInitialize(WSI.egl.display, &egl_major, &egl_minor) == EGL_TRUE);
  assert(egl_major == 1);
  assert(egl_minor >= 5);

  // Switching this to EGL_OPENGL_ES_API fixes rendering.
  if (argc > 1 && strcmp(argv[1], "gles") == 0) {
    eglBindAPI(EGL_OPENGL_ES_API);
  } else {
    eglBindAPI(EGL_OPENGL_API);
  }

  int num_configs = 0;
  eglChooseConfig(WSI.egl.display, config_attribs, configs, 256, &num_configs);
  assert(num_configs > 0);
  WSI.egl.context = eglCreateContext(WSI.egl.display, configs[0],
                                     EGL_NO_CONTEXT, context_attribs);
  assert(WSI.egl.context);

  int screen_num = XDefaultScreen(WSI.display);
  assert(screen_num == 0); // next line wont work for non-zero screens.
  xcb_screen_t *screen =
      xcb_setup_roots_iterator(xcb_get_setup(WSI.connection)).data;
  assert(screen);

  // Create our surface and add xdg_shell roles so it will be displayed.
  WSI.w = 300;
  WSI.h = 300;
  xcb_visualid_t visual;
  eglGetConfigAttrib(WSI.egl.display, configs[0], EGL_NATIVE_VISUAL_ID,
                     &visual);
  xcb_colormap_t cmap = xcb_generate_id(WSI.connection);
  xcb_create_colormap(WSI.connection, XCB_COLORMAP_ALLOC_NONE, cmap,
                      screen->root, visual);

  uint32_t valmask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
  uint32_t valarray[] = {XCB_EVENT_MASK_EXPOSURE, cmap, 0};
  WSI.window = xcb_generate_id(WSI.connection);
  xcb_create_window(WSI.connection, XCB_COPY_FROM_PARENT, WSI.window,
                    screen->root, 0, 0, WSI.w, WSI.h, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, visual, valmask,
                    &valarray[0]);
  xcb_map_window(WSI.connection, WSI.window);
  xcb_flush(WSI.connection);

  // This call corrupts the context.
  eglMakeCurrent(WSI.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 WSI.egl.context);

  WSI.egl.surface = eglCreatePlatformWindowSurface(WSI.egl.display, configs[0],
                                                   &WSI.window, NULL);
  assert(WSI.egl.surface);
  eglMakeCurrent(WSI.egl.display, WSI.egl.surface, WSI.egl.surface,
                 WSI.egl.context);

  // load some opengl functions like mpv.
  CLEARCOLOR ClearColor = (CLEARCOLOR)eglGetProcAddress("glClearColor");
  CLEAR Clear = (CLEAR)eglGetProcAddress("glClear");

  eglSwapInterval(WSI.egl.display, 0);
  while (!should_exit) {
    usleep(10000);
    ClearColor(0.2, 0.4, 0.9, 1.0);
    Clear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(WSI.egl.display, WSI.egl.surface);
  }

  return 0;
}
