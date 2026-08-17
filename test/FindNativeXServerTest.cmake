# SPDX-FileCopyrightText: 2026 SonicDE contributors
# SPDX-License-Identifier: BSD-2-Clause

if(NOT MODULE_DIR)
    message(FATAL_ERROR "MODULE_DIR is required")
endif()

list(PREPEND CMAKE_MODULE_PATH "${MODULE_DIR}")
include(FindNativeXServer)

set(test_root "${CMAKE_CURRENT_BINARY_DIR}/find-native-x-server-test")
set(wrapper_directory "${test_root}/wrapper")
set(binary_directory "${test_root}/binary")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${wrapper_directory}" "${binary_directory}")

file(WRITE "${wrapper_directory}/Xorg" "#!/bin/sh\nexec /usr/lib/Xorg \"$@\"\n")
string(ASCII 127 69 76 70 elf_magic)
file(WRITE "${binary_directory}/Xorg" "${elf_magic}test payload")
file(CHMOD
    "${wrapper_directory}/Xorg"
    "${binary_directory}/Xorg"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

find_native_x_server(result
    HINTS
        "${wrapper_directory}"
        "${binary_directory}"
)
file(REAL_PATH "${binary_directory}/Xorg" expected_result)
if(NOT result STREQUAL expected_result)
    message(FATAL_ERROR "Expected '${expected_result}', got '${result}'")
endif()

file(REMOVE_RECURSE "${test_root}")
