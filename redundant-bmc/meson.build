# SPDX-License-Identifier: Apache-2.0

fs = import('fs')

subdir('src')

if get_option('tests').allowed()
    subdir('test')
endif

rbmc_config_file = get_option('rbmc-config-file')
if rbmc_config_file == ''
    error(
        'Redundant BMC config file must be specified.\n' + 'Check redundant-bmc/config_files/ for available configs.\n' + 'Specify with: -Drbmc-config-file=<filename>',
    )
endif

config_source = join_paths(
    meson.current_source_dir(),
    'config_files',
    rbmc_config_file,
)

# Validate that the specified config file exists
if not fs.is_file(config_source)
    error(
        'Redundant BMC config file not found: @0@\n'.format(config_source) + 'Check redundant-bmc/config_files/ for available configs.\n',
    )
endif

install_data(
    config_source,
    install_dir: join_paths(
        get_option('datadir'),
        'phosphor-state-manager',
        'redundant-bmc',
    ),
    rename: 'config.json',
)
