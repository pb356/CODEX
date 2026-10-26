// Development-only recording of actual pixels from an isolated Xvfb display, no image synthesis.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
int main(int argc, char **argv) {
    int seconds = argc > 1 ? std::atoi(argv[1]) : 15;
    if (seconds < 1 || seconds > 60)
        return 2;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return 3;
    Window root = DefaultRootWindow(display);
    int width = 1600, height = 1000;
    auto start = std::chrono::steady_clock::now();
    for (int n = 0; n < seconds * 10; ++n) {
        XImage *image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
        if (!image || image->bits_per_pixel != 32 || image->bytes_per_line != width * 4)
            return 4;
        bool ok = std::fwrite(image->data, 1, size_t(width) * height * 4, stdout) ==
                  size_t(width) * height * 4;
        XDestroyImage(image);
        if (!ok)
            return 5;
        std::this_thread::sleep_until(start + std::chrono::milliseconds((n + 1) * 100));
    }
    XCloseDisplay(display);
    return 0;
}
