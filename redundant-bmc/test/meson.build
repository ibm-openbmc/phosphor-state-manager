# SPDX-License-Identifier: Apache-2.0

test('rbmc-tests',
  executable('rbmc-tests',
    'role_determination_test.cpp',
    '../src/role_determination.cpp',
    dependencies: [
      gtest,
      sdbusplus,
      phosphordbusinterfaces,
      phosphorlogging,
    ],
    include_directories: '../src'
  )
)
