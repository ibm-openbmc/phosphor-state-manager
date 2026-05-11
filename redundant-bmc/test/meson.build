# SPDX-License-Identifier: Apache-2.0

test(
    'rbmc-tests',
    executable(
        'rbmc-tests',
        'config_parser_test.cpp',
        'error_data_test.cpp',
        'manager_test.cpp',
        'persistent_data_test.cpp',
        'redundancy_test.cpp',
        'role_determination_test.cpp',
        'system_state_test.cpp',
        'util_test.cpp',
        dependencies: [
            gtest,
            gmock,
            nlohmann_json_dep,
            sdbusplus,
            phosphordbusinterfaces,
            phosphorlogging,
        ],
        include_directories: '../src',
        link_with: rbmc_lib,
    ),
)
