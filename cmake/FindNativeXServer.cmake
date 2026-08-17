# SPDX-FileCopyrightText: 2026 SonicDE contributors
# SPDX-License-Identifier: BSD-2-Clause

include(CMakeParseArguments)

# Find the first native ELF Xorg executable in the supplied candidates and
# search directories. Wrapper scripts are deliberately skipped because the
# login manager executes Xorg with ambient capabilities.
function(find_native_x_server output_variable)
    set(options)
    set(one_value_arguments)
    set(multi_value_arguments CANDIDATES HINTS)
    cmake_parse_arguments(PARSE_ARGV 1 XSERVER
        "${options}"
        "${one_value_arguments}"
        "${multi_value_arguments}"
    )

    set(candidate_paths ${XSERVER_CANDIDATES})

    foreach(search_directory IN LISTS XSERVER_HINTS)
        if(NOT search_directory)
            continue()
        endif()

        unset(candidate_path)
        find_program(candidate_path
            NAMES Xorg
            PATHS "${search_directory}"
            NO_DEFAULT_PATH
            NO_CACHE
        )
        if(candidate_path)
            list(APPEND candidate_paths "${candidate_path}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES candidate_paths)
    foreach(candidate_path IN LISTS candidate_paths)
        if(NOT EXISTS "${candidate_path}")
            continue()
        endif()

        file(REAL_PATH "${candidate_path}" candidate_real_path)
        if(NOT EXISTS "${candidate_real_path}")
            continue()
        endif()

        file(READ "${candidate_real_path}" candidate_magic LIMIT 4 HEX)
        string(TOLOWER "${candidate_magic}" candidate_magic)
        if(candidate_magic STREQUAL "7f454c46")
            set(${output_variable} "${candidate_real_path}" PARENT_SCOPE)
            return()
        endif()

        message(STATUS
            "Skipping Xorg candidate '${candidate_path}': "
            "'${candidate_real_path}' is not a native ELF executable"
        )
    endforeach()

    set(${output_variable} "" PARENT_SCOPE)
endfunction()
