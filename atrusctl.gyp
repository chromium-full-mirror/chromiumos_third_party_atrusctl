{
  'target_defaults': {
    'variables': {
      'deps': [
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libudev',
        'libusb-1.0',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'atrusd-adaptors',
      'type': 'none',
      'variables': {
        'dbus_adaptors_out_dir': 'include/atrusctl/dbus_adaptors',
      },
      'sources': [
        'dbus_bindings/org.chromium.Atrusctl.xml',
      ],
      'includes': ['../../platform2/common-mk/generate-dbus-adaptors.gypi'],
    },
    {
      'target_name': 'atrusd',
      'type': 'executable',
      'dependencies': [
        'atrusd-adaptors',
      ],
      'sources': [
        'src/atrusd.cc',
        'src/atrus_controller.cc',
        'src/dbus_adaptor.cc',
        'src/diagnostics.cc',
        'src/hid_connection.cc',
        'src/hid_message.cc',
        'src/hidraw_device.cc',
        'src/udev_device_manager.cc',
        'src/upgrade.cc',
        'src/usb_device.cc',
        'src/usb_dfu_device.cc',
        'src/util.cc'
      ],
    },
  ],
}
