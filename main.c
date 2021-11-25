// clang-format off
// Build this with:
// $ gcc -g -o demo main.c xdg-shell-protocol.c -lwayland-client -lEGL -lwayland-egl -lpthread
// Generate the xdg-shell files from protocols with
// $ wayland-scanner private-code < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > xdg-shell-protocol.c
// $ wayland-scanner client-header < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > xdg-shell-client-protocol.h
// clang-format on

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h" // True suffering is generated code.

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
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_surface *surface;
  struct xdg_wm_base *wm;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    struct wl_egl_window *window;
  } egl;

  int32_t w, h;
};

struct wsi WSI = {0};
static bool should_exit = false;
static bool draw_next_frame = false;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT;
PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;

// xdg_wm_base generic callbacks

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

// xdg_wm_base top level window callbacks.

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel, int32_t w,
                                   int32_t h, struct wl_array *states) {

  // our chosen w/h are already fine.
  if (w == 0 && h == 0)
    return;

  // window resized
  if (WSI.w != w || WSI.h != h) {
    WSI.w = w;
    WSI.h = h;

    wl_egl_window_resize(WSI.egl.window, w, h, 0, 0);
    wl_surface_commit(WSI.surface);
  }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  should_exit = true;
}

struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// wl_registry handling

static void global_registry_handler(void *data, struct wl_registry *registry,
                                    uint32_t id, const char *interface,
                                    uint32_t version) {
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    WSI.compositor =
        wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    WSI.surface = wl_compositor_create_surface(WSI.compositor);
    // create the unused cursor surface.
    wl_compositor_create_surface(WSI.compositor);
  }
  if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    WSI.wm = wl_registry_bind(registry, id, &xdg_wm_base_interface, 2);
    xdg_wm_base_add_listener(WSI.wm, &xdg_wm_base_listener, NULL);
  }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
                                    uint32_t id) {
  // This section deliberately left blank.
}

const struct wl_registry_listener registry_listener = {
    .global = global_registry_handler,
    .global_remove = global_registry_remover,
};

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

// forward declare for for use in the callback.
static const struct wl_callback_listener wl_surface_frame_callback_listener;

static void wl_surface_frame_done(void *data, struct wl_callback *cb,
                                  uint32_t time) {
  // Mark callback handled.
  wl_callback_destroy(cb);

  // Register next frame's callback.
  cb = wl_surface_frame(WSI.surface);
  wl_callback_add_listener(cb, &wl_surface_frame_callback_listener, NULL);
  draw_next_frame = true;
}

static const struct wl_callback_listener wl_surface_frame_callback_listener = {
    .done = wl_surface_frame_done,
};

typedef void(APIENTRYP CLEARCOLOR)(GLclampf red, GLclampf green, GLclampf blue,
                                   GLclampf alpha);
typedef void(APIENTRYP CLEAR)(GLbitfield mask);

void *render_thread(void *data) {}

int main(int argc, char *argv[]) {
  WSI.display = wl_display_connect(NULL);
  assert(WSI.display);
  struct wl_registry *registry = wl_display_get_registry(WSI.display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(WSI.display);

  // Create our surface and add xdg_shell roles so it will be displayed.
  WSI.w = 300;
  WSI.h = 300;
  WSI.xdg_surface = xdg_wm_base_get_xdg_surface(WSI.wm, WSI.surface);
  xdg_surface_add_listener(WSI.xdg_surface, &xdg_surface_listener, NULL);
  WSI.xdg_toplevel = xdg_surface_get_toplevel(WSI.xdg_surface);
  xdg_toplevel_add_listener(WSI.xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title(WSI.xdg_toplevel, "Wayland EGL window");

  struct wl_callback *cb = wl_surface_frame(WSI.surface);
  wl_callback_add_listener(cb, &wl_surface_frame_callback_listener, NULL);

  // initialize EGL for wayland.
  eglCreatePlatformWindowSurfaceEXT =
      (void *)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
  eglGetPlatformDisplayEXT =
      (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
  int egl_major = 0;
  int egl_minor = 0;
  WSI.egl.display =
      eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, WSI.display, NULL);
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
  wl_display_roundtrip(WSI.display);
  wl_surface_set_buffer_scale(WSI.surface, 1);
  wl_surface_commit(WSI.surface);
  wl_display_dispatch(WSI.display);
  wl_display_roundtrip(WSI.display);

  // This call corrupts the context.
  eglMakeCurrent(WSI.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 WSI.egl.context);

  assert(WSI.surface);
  WSI.egl.window = wl_egl_window_create(WSI.surface, WSI.w, WSI.h);
  assert(WSI.egl.window);

  WSI.egl.surface = eglCreatePlatformWindowSurface(WSI.egl.display, configs[0],
                                                   WSI.egl.window, NULL);
  assert(WSI.egl.surface);
  eglMakeCurrent(WSI.egl.display, WSI.egl.surface, WSI.egl.surface,
                 WSI.egl.context);

  // load some opengl functions like mpv.
  CLEARCOLOR ClearColor = (CLEARCOLOR)eglGetProcAddress("glClearColor");
  CLEAR Clear = (CLEAR)eglGetProcAddress("glClear");

  eglSwapInterval(WSI.egl.display, 0);
  while (!should_exit && wl_display_dispatch_pending(WSI.display) != -1) {
    ClearColor(0.2, 0.4, 0.9, 1.0);
    Clear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(WSI.egl.display, WSI.egl.surface);
  }

  return 0;
}
