cmake_minimum_required(VERSION 3.10)
project(wayland_client_gles_demo)

# 设置编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(WLR_USE_UNSTABLE)

# 查找 Wayland 相关库
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_CURSOR REQUIRED wayland-cursor)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES2 REQUIRED glesv2)

# find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
# set(XDG_SHELL_XML /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
# set(XDG_SHELL_HEADER ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h)

# 添加可执行文件
add_executable(wayland_client_gles_demo
    wayland_client_demo.c
    xdg-shell-client-protocol.c
)

# 链接库
target_link_libraries(wayland_client_gles_demo
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_CURSOR_LIBRARIES}
    ${WAYLAND_EGL_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES2_LIBRARIES}
    m
 )

# 包含头文件目录
target_include_directories(wayland_client_gles_demo PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_CURSOR_INCLUDE_DIRS}
    ${WAYLAND_EGL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}

)