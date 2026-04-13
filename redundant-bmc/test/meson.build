# SPDX-License-Identifier: Apache-2.0

test(
    'rbmc-tests',
    executable(
        'rbmc-tests',
        'error_data_test.cpp',
        'persistent_data_test.cpp',
        'redundancy_test.cpp',
        'role_determination_test.cpp',
        dependencies: [
            gtest,
            gmock,
            nlohmann_json_dep,
            sdbusplus,
            phosphordbusinterfaces,
            phosphorlogging,
        ],
        link_with: rbmc_lib,
        include_directories: ['../src', 'mocks'],
    ),
)
