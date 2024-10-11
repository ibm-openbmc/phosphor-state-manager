# SPDX-License-Identifier: Apache-2.0
execinstalldir = join_paths(get_option('libexecdir'), 'phosphor-state-manager')

ssl_dep = dependency('openssl')

rbmc_deps = [
    nlohmann_json_dep,
    phosphordbusinterfaces,
    phosphorlogging,
    sdbusplus,
    ssl_dep,
]

executable('phosphor-rbmc-state-manager',
           'active_role_handler.cpp',
           'manager.cpp',
           'passive_role_handler.cpp',
           'rbmc_manager_main.cpp',
           'role_determination.cpp',
           'persistent_data.cpp',
           'services_impl.cpp',
           'sibling_impl.cpp',
    dependencies: rbmc_deps,
    install: true,
    install_dir: execinstalldir,
)

executable('rbmctool',
    'rbmc_tool.cpp',
    'services_impl.cpp',
    dependencies: [rbmc_deps, CLI11],
    install: true
)
