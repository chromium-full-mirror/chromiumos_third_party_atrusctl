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
      'target_name': 'atrusd',
      'type': 'executable',
      'sources': [
        'src/atrusd.cc',
        'src/diagnose.cc',
        'src/upgrade.cc',
        'src/usb_device.cc',
        'src/usb_dfu_device.cc',
        'src/util.cc',
      ],
    },
  ],
}
