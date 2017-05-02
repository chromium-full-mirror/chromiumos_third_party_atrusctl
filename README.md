# atrusctl
This tool is used to interact with an Atrus device.

## Requirements
The GNU C Library, libusb 1.0 and libudev are required.
GNU C is required because the program utilizes `argp`.

## Building
CMake is used for building the application.

Here is an example of how to build:
```
$ mkdir build/
$ cd build/
$ cmake ..
$ make
```
You can also install it on your machine by running:
```
$ make install
```

## Using
Run the following to see the help section of the application.
```
$ atrusd --help
```

Also, make sure you have read and write access to the USB device.
This may be done by, e.g., setting appropriate [udev](https://www.kernel.org/pub/linux/utils/kernel/hotplug/udev/udev.html) rules.

## License
Individual files are tagged with SPDX-License-Identifier to indicate its license instead of including the full license text.
See the [SPDX License List](https://spdx.org/licenses/) for more information.
