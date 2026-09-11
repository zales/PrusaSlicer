# range-v3 0.12.0 forward declares the std containers for every Apple clang, a
# workaround meant for toolchains older than clang 6. On current Xcode those
# declarations clash with Boost.Container's own ones in
# boost/container/detail/std_fwd.hpp and the build stops with
# "redefinition of 'allocator' as different kind of symbol".
# Keep the workaround for clang < 6 only. Runs in the extracted source tree.
set(meta_file "include/meta/meta.hpp")
set(old_guard "#if defined(__apple_build_version__) || (defined(__clang__) && __clang_major__ < 6)")
set(new_guard "#if defined(__clang__) && __clang_major__ < 6")

file(READ "${meta_file}" content)
string(FIND "${content}" "${old_guard}" old_pos)
if(old_pos EQUAL -1)
    string(FIND "${content}" "${new_guard}" new_pos)
    if(new_pos EQUAL -1)
        message(FATAL_ERROR "range-v3 patch: std forward declaration guard not found in ${meta_file}")
    endif()
    return()
endif()
string(REPLACE "${old_guard}" "${new_guard}" content "${content}")
file(WRITE "${meta_file}" "${content}")
