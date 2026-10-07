# audio_playback generates a tone, so it is the one example needing libm.
m_dep = meson.get_compiler('c').find_library('m', required: false)

executable('audio_capture', 'audio_capture.c',
  dependencies: [pipewire_flow_dep],
)

executable('video_capture', 'video_capture.c',
  dependencies: [pipewire_flow_dep],
)

executable('video_capture_dmabuf', 'video_capture_dmabuf.c',
  dependencies: [pipewire_flow_dep],
)

executable('video_capture_mjpeg', 'video_capture_mjpeg.c',
  dependencies: [pipewire_flow_dep],
)

executable('audio_playback', 'audio_playback.c',
  dependencies: [pipewire_flow_dep, m_dep],
)

executable('stream_manual_link', 'stream_manual_link.c',
  dependencies: [pipewire_flow_dep],
)

executable('list_targets', 'list_targets.c',
  dependencies: [pipewire_flow_dep],
)

executable('filter_mix', 'filter_mix.c',
  dependencies: [pipewire_flow_dep],
)

executable('filter_signal_port', 'filter_signal_port.c',
  dependencies: [pipewire_flow_dep],
)

executable('filter_event_port', 'filter_event_port.c',
  dependencies: [pipewire_flow_dep],
)

executable('filter_dmabuf_bundle', 'filter_dmabuf_bundle.c',
  dependencies: [pipewire_flow_dep],
)

executable('filter_port_link', 'filter_port_link.c',
  dependencies: [pipewire_flow_dep],
)
