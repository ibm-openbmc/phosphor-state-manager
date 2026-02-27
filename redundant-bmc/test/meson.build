# SPDX-License-Identifier: Apache-2.0

test(
    'rbmc-tests',
    executable(
        'rbmc-tests',
        'error_data_test.cpp',
        'persistent_data_test.cpp',
        'redundancy_test.cpp',
        'role_determination_test.cpp',
        '../src/active_role_handler.cpp',
        '../src/error_data.cpp',
        '../src/manager.cpp',
        '../src/passive_role_handler.cpp',
        '../src/persistent_data.cpp',
        '../src/redundancy.cpp',
        '../src/redundancy_interface.cpp',
        '../src/redundancy_mgr.cpp',
        '../src/role_determination.cpp',
        dependencies: [
            gtest,
            gmock,
            nlohmann_json_dep,
            sdbusplus,
            phosphordbusinterfaces,
            phosphorlogging,
        ],
        include_directories: ['../src', 'mocks'],
    ),
)
