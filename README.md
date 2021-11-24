This is a simple example of how to create an EGL window on wayland. It differs
from some other examples in supporting resizing.

You will need to install the `wayland-scanner` application to create the
protocol code for the xdg-shell protocol extension. If you do not have the
protocol specification (xml) you can typically install it via a package called
`wayland-protocols`. Build instructions are at the top of `main.c`

This code is dual licensed under either the MIT or the CC0 license, whichever
you prefer.
