# SPDX-License-Identifier: Apache-2.0

test(
    'rbmc-tests',
    executable(
        'rbmc-tests',
        'error_data_test.cpp',
        'persistent_data_test.cpp',
        'redundancy_test.cpp',
        'role_determination_test.cpp',
        '../src/error_data.cpp',
        '../src/persistent_data.cpp',
        '../src/redundancy.cpp',
        '../src/role_determination.cpp',
        dependencies: [
            gtest,
            nlohmann_json_dep,
            sdbusplus,
            phosphordbusinterfaces,
            phosphorlogging,
        ],
        include_directories: '../src',
    ),
)
