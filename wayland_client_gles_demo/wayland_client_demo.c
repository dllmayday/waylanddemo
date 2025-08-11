/*
 * wayland-egl-demo.c
 * Minimal Wayland client that creates a toplevel window using xdg-shell
 * and renders a rotating clear color using GLES2 + EGL via wl_egl_window.
 *
 * This variant requests server-side decorations via xdg-decoration (zxdg).
 *
 * Build example:
 *   gcc -o wayland-egl-demo wayland-egl-demo.c `pkg-config --cflags --libs wayland-client wayland-egl egl glesv2` -lm
 *
 * Generate protocol headers:
 *   wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
 *   wayland-scanner client-header /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-unstable-v1-client-protocol.h
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* Generated headers from wayland-scanner */
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h" /* for zxdg_* */

/* Globals (for demo simplicity) */
static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm = NULL;

/* decoration globals */
static struct zxdg_decoration_manager_v1 *decoration_manager = NULL;
static struct zxdg_toplevel_decoration_v1 *toplevel_decoration = NULL;

static struct wl_surface *wl_surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;

/* EGL / GL objects */
static struct wl_egl_window *egl_window = NULL;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static EGLConfig egl_config = NULL;

static int width = 640;
static int height = 480;
static bool configured = false;

/* Forward */
static void create_egl();
static void destroy_egl();

/* xdg_wm_base ping handler */
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

/* xdg_surface / toplevel configure */
static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    /* A decorate-mode change from compositor will come as an xdg_surface.configure;
       the client should acknowledge configure after updating content appropriately. */
    xdg_surface_ack_configure(surface, serial);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {
    if (w > 0 && h > 0) {
        width = w;
        height = h;
        if (egl_window) wl_egl_window_resize(egl_window, width, height, 0, 0);
        if (egl_display != EGL_NO_DISPLAY && egl_surface != EGL_NO_SURFACE) glViewport(0, 0, width, height);
    }
    configured = true;
}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    /* The compositor requested our window to close. Exit. */
    exit(0);
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

/* zxdg decoration listener: compositor tells us which mode it chose */
static void decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode) {
    /* mode is one of ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
       or ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE (and possibly others).
       If compositor picks SERVER_SIDE, compositor will draw titlebar/borders.
       If it picks CLIENT_SIDE, app must draw its own decorations (fallback).
    */
    if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
        fprintf(stderr, "Decoration: compositor chose SERVER_SIDE (use SSD)\n");
        /* If compositor chose server-side decoration, the compositor will
           expect the client NOT to draw its own titlebar. Typically you'd
           resize/redraw content accordingly and ack configure. */
    } else if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
        fprintf(stderr, "Decoration: compositor chose CLIENT_SIDE (fallback to CSD)\n");
    } else {
        fprintf(stderr, "Decoration: unknown mode %u\n", mode);
    }
}
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = decoration_configure,
};

/* Registry handler: bind compositor, xdg_wm_base and decoration manager */
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        /* bind decoration manager (version 1) */
        decoration_manager = wl_registry_bind(registry, id, &zxdg_decoration_manager_v1_interface, 1);
    }
}
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* Create Wayland surface + xdg objects */
static void create_window() {
    wl_surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm, wl_surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "wayland-egl-demo (with xdg-decoration request)");

    /* If decoration manager was advertised, create decoration object and request SSD */
    if (decoration_manager) {
        toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(toplevel_decoration, &decoration_listener, NULL);

        /* Request server-side decorations (compositor may accept or ignore). */
        zxdg_toplevel_decoration_v1_set_mode(toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        /* Note: after requesting a mode, compositor will emit xdg_surface.configure. */
    }

    wl_surface_commit(wl_surface);
}

static void create_egl() {
    EGLint major, minor;
    EGLint n;
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        exit(1);
    }
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(1);
    }

    if (!eglChooseConfig(egl_display, attribs, NULL, 0, &n) || n == 0) {
        fprintf(stderr, "No EGL configs\n");
        exit(1);
    }
    EGLConfig *configs = calloc(n, sizeof(EGLConfig));
    eglChooseConfig(egl_display, attribs, configs, n, &n);
    egl_config = configs[0];
    free(configs);

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(1);
    }

    egl_window = wl_egl_window_create(wl_surface, width, height);
    if (!egl_window) {
        fprintf(stderr, "Failed to create wl_egl_window\n");
        exit(1);
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL window surface\n");
        exit(1);
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(1);
    }

    glViewport(0, 0, width, height);
}

static void destroy_egl() {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        eglTerminate(egl_display);
    }
    if (egl_window) {
        wl_egl_window_destroy(egl_window);
        egl_window = NULL;
    }
    egl_display = EGL_NO_DISPLAY;
    egl_surface = EGL_NO_SURFACE;
    egl_context = EGL_NO_CONTEXT;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !xdg_wm) {
        fprintf(stderr, "Compositor or xdg_wm_base not available\n");
        return 1;
    }

    create_window();

    /* initial roundtrip so compositor can configure us */
    wl_display_roundtrip(display);

    /* create EGL after we've created the surface; use initial width/height */
    create_egl();

    /* Main loop: render color that changes with time */
    double t = 0.0;
    while (true) {
        /* simple animation */
        t += 0.016;
        float r = (sin(t) * 0.5f) + 0.5f;
        float g = (sin(t + 2.0) * 0.5f) + 0.5f;
        float b = (sin(t + 4.0) * 0.5f) + 0.5f;

        glViewport(0, 0, width, height);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        eglSwapBuffers(egl_display, egl_surface);

        /* Dispatch Wayland events (non-blocking) */
        wl_display_dispatch_pending(display);
        wl_display_flush(display);

        /* Sleep ~16ms */
        usleep(16000);
    }

    /* cleanup (never reached in this demo loop) */
    if (toplevel_decoration) {
        zxdg_toplevel_decoration_v1_destroy(toplevel_decoration);
        toplevel_decoration = NULL;
    }
    if (decoration_manager) {
        zxdg_decoration_manager_v1_destroy(decoration_manager);
        decoration_manager = NULL;
    }

    destroy_egl();

    if (xdg_toplevel) xdg_toplevel_destroy(xdg_toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (wl_surface) wl_surface_destroy(wl_surface);
    if (xdg_wm) xdg_wm_base_destroy(xdg_wm);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);

    return 0;
}
