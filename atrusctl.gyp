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
        'src/crc32.cc',
        'src/diagnose.cc',
        'src/upgrade.cc',
        'src/util.cc',
      ],
    },
  ],
}

