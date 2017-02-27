add_definitions(-D_GNU_SOURCE)

add_executable(atrusctl
               atrusctl.c
               crc32.c
               daemon.c
               diagnose.c
               list.c
               upgrade.c
               util.c)

find_library(libudev_LIBRARIES NAMES udev)
find_library(libusb_LIBRARIES NAMES usb-1.0)
target_link_libraries(atrusctl
                      ${libudev_LIBRARIES}
                      ${libusb_LIBRARIES})

install(TARGETS atrusctl DESTINATION sbin)
