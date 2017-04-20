add_definitions(-D_GNU_SOURCE)

add_executable(atrusctl
               atrusctl.cc
               crc32.cc
               daemon.cc
               diagnose.cc
               list.cc
               upgrade.cc
               util.cc)

find_library(libudev_LIBRARIES NAMES udev)
find_library(libusb_LIBRARIES NAMES usb-1.0)
target_link_libraries(atrusctl
                      ${libudev_LIBRARIES}
                      ${libusb_LIBRARIES})

install(TARGETS atrusctl DESTINATION sbin)
