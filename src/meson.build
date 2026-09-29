tinypipewire_sources = files(
  'tpw_log.c',
  'tpw_pw_core.c',
  'tpw_pw_dl.c',
  'tpw_spa_format.c',
  'tpw_stream.c',
  'tpw_stream_playback.c',
  'tpw_stream_routing.c',
  'tpw_buffer.c',
  'tpw_audio_format.c',
  'tpw_video_format.c',
  'tpw_video_enum.c',
  'tpw_filter.c',
  'tpw_filter_port.c',
  'tpw_filter_process.c',
  'tpw_filter_event.c',
  'tpw_filter_dmabuf.c',
  'tpw_filter_port_link.c',
  'tpw_dmabuf.c',
  'tpw_rate_limit.c',
  'tpw_stream_dmabuf.c',
)

# libpipewire is opened at run time (see tpw_pw_dl.h), so the library
# compiles against its headers but does not link it. Only its compile flags
# are taken, which also keeps it out of the generated pkg-config file.
pipewire_headers_dep = declare_dependency(
  compile_args: [
    '-I' + pipewire_dep.get_variable(pkgconfig: 'includedir') / 'pipewire-0.3',
    '-I' + dependency('libspa-0.2').get_variable(pkgconfig: 'includedir') / 'spa-0.2',
  ],
)

tinypipewire_lib = library('tinypipewire',
  tinypipewire_sources,
  include_directories: tpw_inc,
  dependencies: [pipewire_headers_dep, dependency('dl'), dependency('threads')],
  gnu_symbol_visibility: 'hidden',
  soversion: meson.project_version().split('.')[0],
  install: true,
)
