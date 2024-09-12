# SPDX-License-Identifier: Apache-2.0

test('rbmc-tests',
  executable('rbmc-tests',
    'persistent_data_test.cpp',
    'role_determination_test.cpp',
    '../src/persistent_data.cpp',
    '../src/role_determination.cpp',
    dependencies: [
      gtest,
      sdbusplus,
      phosphordbusinterfaces,
      phosphorlogging,
      nlohmann_json_dep,
    ],
    include_directories: '../src'
  )
)
