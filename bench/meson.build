# Footprint comparison pair: the same capture workload through tinypipewire
# and through raw pw_stream, plus bench_idle for the cost around a stream's
# lifetime. bench/footprint.sh drives all three.
executable('bench_tpw', 'bench_tpw.c',
  dependencies: [tinypipewire_dep],
)

executable('bench_raw', 'bench_raw.c',
  dependencies: [pipewire_dep],
)

executable('bench_idle', 'bench_idle.c',
  dependencies: [tinypipewire_dep],
)
