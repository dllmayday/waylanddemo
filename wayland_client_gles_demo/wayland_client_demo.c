/*
 * wayland-egl-demo.c
 * Minimal Wayland client that creates a toplevel window using xdg-shell
 * and renders a rotating clear color using GLES2 + EGL via wl_egl_window.
 *
 * Requirements:
 *  - wayland-client
 *  - wayland-protocols (for xdg-shell)
 *  - wayland-egl
 *  - EGL
 *  - OpenGL ES 2.0
 *
 * You must generate xdg-shell-client-protocol.h from the protocol XML shipped
 * with wayland-protocols. See the README section below for commands.
 *
 * Build (example):
 *   gcc -o wayland-egl-demo wayland-egl-demo.c `pkg-config --cflags --libs wayland-client wayland-egl egl glesv2` -lm
 *
 * Run under a Wayland compositor (e.g. Weston, Sway, GNOME on Wayland):
 *   ./wayland-egl-demo
 *
 * README: how to obtain the xdg-shell header
 *  - Install wayland-protocols (package name differs by distro).
 *  - Use wayland-scanner to generate the header:
 *      wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
 *    adjust the path to xdg-shell.xml depending on your distro.
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

/* Generated header from xdg-shell.xml (see README above) */
#include "xdg-shell-client-protocol.h"

/* Globals (for demo simplicity) */
static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm = NULL;

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
    xdg_surface_ack_configure(surface, serial);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {
    if (w > 0 && h > 0) {
        width = w;
        height = h;
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

/* Registry handler: bind compositor and xdg_wm_base */
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm, &xdg_wm_base_listener, NULL);
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
    xdg_toplevel_set_title(xdg_toplevel, "wayland-egl-demo");

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
