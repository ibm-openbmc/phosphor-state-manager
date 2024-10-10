# SPDX-License-Identifier: Apache-2.0
execinstalldir = join_paths(get_option('libexecdir'), 'phosphor-state-manager')

executable('phosphor-rbmc-state-manager',
           'active_role_handler.cpp',
           'manager.cpp',
           'passive_role_handler.cpp',
           'rbmc_manager_main.cpp',
           'role_determination.cpp',
           'services_impl.cpp',
           'sibling_impl.cpp',
            dependencies: [
                phosphordbusinterfaces,
                phosphorlogging,
                sdbusplus,
            ],
    install: true,
    install_dir: execinstalldir,
)
