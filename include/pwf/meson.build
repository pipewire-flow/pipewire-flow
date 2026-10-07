pwf_version_parts = meson.project_version().split('.')

pwf_version_cfg = configuration_data()
pwf_version_cfg.set('PWF_VERSION_MAJOR', pwf_version_parts[0])
pwf_version_cfg.set('PWF_VERSION_MINOR', pwf_version_parts[1])
pwf_version_cfg.set('PWF_VERSION_PATCH', pwf_version_parts[2])

configure_file(
  input: 'pwf_version.h.in',
  output: 'pwf_version.h',
  configuration: pwf_version_cfg,
  install: true,
  install_dir: get_option('includedir') / 'pwf',
)
