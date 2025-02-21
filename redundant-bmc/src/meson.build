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

executable(
    'phosphor-rbmc-state-manager',
    'active_role_handler.cpp',
    'manager.cpp',
    'passive_role_handler.cpp',
    'persistent_data.cpp',
    'rbmc_manager_main.cpp',
    'redundancy.cpp',
    'redundancy_interface.cpp',
    'redundancy_mgr.cpp',
    'role_determination.cpp',
    'services_impl.cpp',
    'sibling_impl.cpp',
    'sibling_reset_impl.cpp',
    dependencies: rbmc_deps,
    install: true,
    install_dir: execinstalldir,
)

executable(
    'rbmctool',
    'persistent_data.cpp',
    'rbmc_tool.cpp',
    'services_impl.cpp',
    'sibling_reset_impl.cpp',
    dependencies: [rbmc_deps, CLI11],
    install: true,
)
