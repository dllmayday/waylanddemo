# wayland client gles demo

## Introduction
    使用 wl_egl_window + EGL + GLES2 在 Wayland 上创建一个顶层窗口并渲染简单的动态清屏色

## Build
```
mkdir build & cd build
cmake ..
make
```


## Create wayland protocol headers and source file
```
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner client-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.c
```


## Run
```
./wayland_client_gles_demo
```

## References

Here are some related projects and resources that you might find useful:
- [wayland tutorials](https://wayland-book.com/)
- [simple-egl-wayland.c](https://cgit.freedesktop.org/wayland/weston/tree/clients/simple-egl-wayland.c)
- [weston simple-shm.c ](https://cgit.freedesktop.org/wayland/weston/tree/clients/simple-shm.c)
- [wayland_client_demo](https://github.com/ds-hwang/wayland_client_demo)


