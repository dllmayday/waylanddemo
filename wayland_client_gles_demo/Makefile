WAYLAND-CLIENT=`pkg-config wayland-client --cflags --libs`
CPP=clang++
CXX=clang++
CPPFLAGS?=-Wall -Werror -fvisibility=hidden -std=gnu++11

all: wayland_client_demo

wayland_client_demo: wayland_client_demo.o
	$(CPP) -o wayland_client_demo *.o $(WAYLAND-CLIENT)

clean:
	$(RM) *.o wayland_client_demo
