# Native platform property identifiers stay inside their own backend.
#
# The Windows backend reads its device facts through DEVPKEY_* property keys.
# Those names are an implementation detail of one operating system: they mean
# nothing on another platform, and put in front of a user they read as noise.
# The windows-device-inventory spec therefore requires that they exist ONLY
# inside the Windows backend directory, and that no field identifier or rendered
# label anywhere carries a native property-key prefix.
#
# This runs as a CTest test on every platform (CMake script mode, so it needs no
# shell) — the rule is checked by the Linux CI that gates this project, not only
# by the Windows job that could produce a violation.
#
# A line that must name a native key in order to assert it is REJECTED — the
# closed-vocabulary tests do exactly that — opts out with a trailing
# `native-key-guard: allow` comment. Per line, never per file, so an exemption
# stays as narrow as the reason for it.
#
# Invoke with -DDEVMGR_SOURCE_DIR=<repo root>.

if(NOT DEVMGR_SOURCE_DIR)
    message(FATAL_ERROR "check-native-keys.cmake needs -DDEVMGR_SOURCE_DIR=<repo root>")
endif()

# Where native Windows property identifiers ARE allowed to appear.
set(_allowed_dir "${DEVMGR_SOURCE_DIR}/platform/windows/")
set(_allow_marker "native-key-guard: allow")

# Prefixes that name a native Windows property key or property type. Matched
# with the trailing underscore, so prose ABOUT the rule — a comment, or a test
# asserting that no label contains "DEVPKEY" — is not itself a violation.
set(_native_prefixes "DEVPKEY_" "DEVPROPKEY_" "DEVPROP_TYPE_")

set(_searched core app gui tui cli daemon tests platform)

set(_violations "")
foreach(_dir IN LISTS _searched)
    file(GLOB_RECURSE _files
        "${DEVMGR_SOURCE_DIR}/${_dir}/*.cpp"
        "${DEVMGR_SOURCE_DIR}/${_dir}/*.hpp")
    foreach(_file IN LISTS _files)
        string(FIND "${_file}" "${_allowed_dir}" _inside_backend)
        if(NOT _inside_backend EQUAL -1)
            continue()
        endif()
        file(READ "${_file}" _content)
        # Escape first: an unescaped semicolon would split one source line into
        # several list items and separate a violation from its allow marker.
        string(REPLACE ";" "\\;" _content "${_content}")
        string(REPLACE "\n" ";" _lines "${_content}")
        set(_number 0)
        foreach(_line IN LISTS _lines)
            math(EXPR _number "${_number} + 1")
            string(FIND "${_line}" "${_allow_marker}" _exempt)
            if(NOT _exempt EQUAL -1)
                continue()
            endif()
            foreach(_prefix IN LISTS _native_prefixes)
                string(FIND "${_line}" "${_prefix}" _hit)
                if(NOT _hit EQUAL -1)
                    list(APPEND _violations "${_file}:${_number}: contains ${_prefix}")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()

# The shared detail-field vocabulary is the one place labels and field
# identifiers are authored, so it is checked directly as well as by the sweep
# above: a label or key carrying a native prefix would put one on a screen.
file(READ "${DEVMGR_SOURCE_DIR}/core/src/device_detail_fields.cpp" _vocabulary)
foreach(_prefix IN LISTS _native_prefixes "Device_")
    string(FIND "${_vocabulary}" "\"${_prefix}" _hit)
    if(NOT _hit EQUAL -1)
        list(APPEND _violations
             "core/src/device_detail_fields.cpp: a label or field key starts with ${_prefix}")
    endif()
endforeach()

if(_violations)
    string(REPLACE ";" "\n  " _report "${_violations}")
    message(FATAL_ERROR
        "Native Windows property identifiers must not appear outside "
        "platform/windows/:\n  ${_report}")
endif()

message(STATUS "NATIVE KEY GUARD OK — no native property identifier outside platform/windows/")
