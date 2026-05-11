# SPDX-License-Identifier: Apache-2.0
execinstalldir = join_paths(get_option('libexecdir'), 'phosphor-state-manager')

ssl_dep = dependency('openssl')

gpiodcxx = dependency('libgpiodcxx', fallback: ['libgpiod', 'gpiodcxx_dep'])

rbmc_deps = [
    gpiodcxx,
    nlohmann_json_dep,
    phosphordbusinterfaces,
    phosphorlogging,
    sdbusplus,
    ssl_dep,
]

rbmc_lib = static_library(
    'rbmc',
    'active_role_handler.cpp',
    'config_parser.cpp',
    'error_data.cpp',
    'gpio.cpp',
    'manager.cpp',
    'passive_role_handler.cpp',
    'persistent_data.cpp',
    'redundancy.cpp',
    'redundancy_interface.cpp',
    'redundancy_mgr.cpp',
    'role_determination.cpp',
    'services_impl.cpp',
    'sibling_impl.cpp',
    'sibling_reset_impl.cpp',
    'sync_interface_impl.cpp',
    'system_state.cpp',
    'util.cpp',
    dependencies: rbmc_deps,
)

executable(
    'phosphor-rbmc-state-manager',
    'rbmc_manager_main.cpp',
    dependencies: rbmc_deps,
    link_with: rbmc_lib,
    install: true,
    install_dir: execinstalldir,
)

executable(
    'rbmctool',
    'rbmc_tool.cpp',
    dependencies: [rbmc_deps, CLI11],
    link_with: rbmc_lib,
    install: true,
)

scripts = ['scripts/wait-for-active-passive-target.sh']
install_data(scripts, install_mode: 'rwxr-xr-x', install_dir: execinstalldir)
