add_definitions(-D_GNU_SOURCE)

add_executable(atrusd
               atrusd.cc
               crc32.cc
               diagnose.cc
               upgrade.cc
               util.cc)

find_library(libudev_LIBRARIES NAMES udev)
find_library(libusb_LIBRARIES NAMES usb-1.0)
target_link_libraries(atrusd
                      ${libudev_LIBRARIES}
                      ${libusb_LIBRARIES})

install(TARGETS atrusd DESTINATION sbin)
