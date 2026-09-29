# Footprint comparison pair: the same capture workload through tinypipewire
# and through raw pw_stream. bench/footprint.sh drives both.
executable('bench_tpw', 'bench_tpw.c',
  dependencies: [tinypipewire_dep],
)

executable('bench_raw', 'bench_raw.c',
  dependencies: [pipewire_dep],
)
