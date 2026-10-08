project('pipewire-flow', 'c',
  version: '0.12.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

# SPA headers use glibc extensions (newlocale, ssize_t) that require _GNU_SOURCE.
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# 0.3.50 is where pw_buffer.requested landed; playback needs it to size a cycle.
pipewire_dep = dependency('libpipewire-0.3', version: '>=0.3.50')

pwf_inc = include_directories('include')

subdir('include/pwf')
subdir('src')

pipewire_flow_dep = declare_dependency(
  link_with: pipewire_flow_lib,
  include_directories: pwf_inc,
  dependencies: [pipewire_dep],
)

install_headers(
  'include/pwf/pwf_stream.h',
  'include/pwf/pwf_filter.h',
  'include/pwf/pwf_log.h',
  'include/pwf/pwf_types.h',
  'include/pwf/pwf_export.h',
  subdir: 'pwf',
)

pkg = import('pkgconfig')
pkg.generate(
  pipewire_flow_lib,
  name: 'pipewire-flow',
  description: 'PipeWire audio/video capture, playback and multi-port filters',
  requires: 'libpipewire-0.3',
)

if get_option('tests')
  # Hardware tests need a real capture device, so a plain `meson test` skips
  # the whole suite; run it with `meson test --suite hardware`.
  add_test_setup('default', exclude_suites: ['hardware'], is_default: true)
  subdir('tests')
endif

if get_option('examples')
  subdir('examples')
endif

if get_option('utils')
  subdir('utils')
endif
