# macOS (Apple Silicon) and iOS (CyPort) build of a rayshader Red* RED4ext plugin, against the macOS RED4ext SDK. Shared by RedData,
# RedFileSystem and RedHttpClient (the same file in the three repositories). Pattern: mod_settings macos/CMakeLists.txt.
#
# The including CMakeLists.txt sets, before include(Common.cmake):
#   REDMAC_NAME            plugin and dylib name (RedData)
#   REDMAC_ENTRY           the exported <plugin>_registerNatives name
#   REDMAC_RTTI_ID_BAND    this plugin's RTTI async id band (MacVault.cpp)
#   REDMAC_SERIAL_SLOT     this plugin's serializable id slot (MacVault.cpp)
# and calls redmac_plugin(<sources...>) after it.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")
# Also for the fetched libraries: the dylib exports the RED4ext entry points and nothing else.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT APPLE)
    message(FATAL_ERROR "This target builds the macOS / iOS dylib; use the top-level CMakeLists.txt on Windows")
endif()

set(REDMAC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
get_filename_component(REDMAC_ROOT "${REDMAC_ROOT}" ABSOLUTE)

set(RED4EXT_SDK_DIR "${REDMAC_ROOT}/../RED4ext.SDK-macos" CACHE PATH "macOS RED4ext SDK checkout (branch mac-next)")
get_filename_component(RED4EXT_SDK_DIR "${RED4EXT_SDK_DIR}" ABSOLUTE)
set(SDK_INCLUDE "${RED4EXT_SDK_DIR}/include")
if(NOT EXISTS "${SDK_INCLUDE}/RED4ext/Relocation-inl.hpp")
    message(FATAL_ERROR "no RED4ext SDK headers at ${SDK_INCLUDE} (set -DRED4EXT_SDK_DIR=...)")
endif()

set(REDLIB_DIR "${REDMAC_ROOT}/deps/RedLib" CACHE PATH "psiberx RedLib checkout (the submodule)")
if(NOT EXISTS "${REDLIB_DIR}/include/RedLib.hpp")
    message(FATAL_ERROR "no RedLib at ${REDLIB_DIR} (git submodule update --init deps/RedLib)")
endif()

# Reproducible: no checkout path ends up in the dylib.
add_compile_options("-ffile-prefix-map=${REDMAC_ROOT}=${REDMAC_NAME}" "-ffile-prefix-map=${RED4EXT_SDK_DIR}=RED4ext.SDK-macos"
                    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")

# ---------------------------------------------------------------------------------------------------------------------------------
# The Mac SDK address table (RED4ext/Relocation-inl.hpp) with this plugin's rows injected (MacSdkRows.inc), and a trap for an
# unmapped hash. Written to a generated copy that comes first on the include path; the SDK checkout is not touched.
set(SDK_RELOC "${SDK_INCLUDE}/RED4ext/Relocation-inl.hpp")
set(SDK_ROWS_INC "${CMAKE_CURRENT_SOURCE_DIR}/MacSdkRows.inc")
set(SDK_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/sdk-overlay")
file(READ "${SDK_RELOC}" SDK_RELOC_TEXT)
file(READ "${SDK_ROWS_INC}" SDK_ROWS_TEXT)
set(SDK_ROWS_MARKER "        { 4201325776, 0x109BAA8 },  // CGlobalFunction_ctor\n")
string(FIND "${SDK_RELOC_TEXT}" "${SDK_ROWS_MARKER}" SDK_ROWS_POS)
if(SDK_ROWS_POS EQUAL -1)
    message(FATAL_ERROR "the anchor row (CGlobalFunction_ctor) is not in ${SDK_RELOC}. Update the anchor in Common.cmake.")
endif()
string(REPLACE "${SDK_ROWS_MARKER}" "${SDK_ROWS_MARKER}${SDK_ROWS_TEXT}" SDK_RELOC_TEXT "${SDK_RELOC_TEXT}")

# An unmapped hash must never resolve to 0 (the caller would jump to null): the unknown-hash tail of the Mac Resolve becomes a call
# into MacTrap.cpp; the returned stub logs once and returns zero.
set(SDK_TAIL_START "    // Address not found or placeholder - return a non-zero sentinel value")
set(SDK_TAIL_END "    return 0;\n#else\n    if constexpr (Detail::AddressResolverOverride<uint32_t>::value)")
string(FIND "${SDK_RELOC_TEXT}" "${SDK_TAIL_START}" SDK_TAIL_A)
string(FIND "${SDK_RELOC_TEXT}" "${SDK_TAIL_END}" SDK_TAIL_B)
if(SDK_TAIL_A EQUAL -1 OR SDK_TAIL_B EQUAL -1)
    message(FATAL_ERROR "the unknown-hash tail of Resolve was not found in ${SDK_RELOC}. Update the anchors in Common.cmake.")
endif()
string(SUBSTRING "${SDK_RELOC_TEXT}" 0 ${SDK_TAIL_A} SDK_HEAD)
math(EXPR SDK_REST_FROM "${SDK_TAIL_B} + 14")
string(SUBSTRING "${SDK_RELOC_TEXT}" ${SDK_REST_FROM} -1 SDK_REST)
set(SDK_RELOC_TEXT "${SDK_HEAD}    // Trap: logs on the first call of the returned stub, then returns 0. Never returns 0 itself.\n    return redmac_trap_for_hash(aHash);\n${SDK_REST}")
set(SDK_DECL_MARKER "extern \"C\" uintptr_t tweakxl_class_vslot(uint32_t aByteOffset);\n")
string(FIND "${SDK_RELOC_TEXT}" "${SDK_DECL_MARKER}" SDK_DECL_POS)
if(SDK_DECL_POS EQUAL -1)
    message(FATAL_ERROR "the extern declaration anchor was not found in ${SDK_RELOC}.")
endif()
string(REPLACE "${SDK_DECL_MARKER}" "${SDK_DECL_MARKER}extern \"C\" uintptr_t redmac_trap_for_hash(uint32_t aHash);\n" SDK_RELOC_TEXT "${SDK_RELOC_TEXT}")
file(WRITE "${SDK_OVERLAY}/RED4ext/Relocation-inl.hpp" "${SDK_RELOC_TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SDK_RELOC}" "${SDK_ROWS_INC}")

# ---------------------------------------------------------------------------------------------------------------------------------
# RedLib with the Mac patch (redlib-mac.patch: RunPending, the global function publishing). RedLib's headers include each other with
# quoted relative paths, so a patched header cannot shadow the submodule's one from an include path: the build uses a patched copy
# in the build folder. The submodule is not edited.
set(REDLIB_COPY "${CMAKE_CURRENT_BINARY_DIR}/redlib")
set(REDLIB_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/redlib-mac.patch")
file(REMOVE_RECURSE "${REDLIB_COPY}")
file(COPY "${REDLIB_DIR}/include/" DESTINATION "${REDLIB_COPY}")
execute_process(COMMAND /usr/bin/patch -p1 -s -N -i "${REDLIB_PATCH}" WORKING_DIRECTORY "${REDLIB_COPY}" RESULT_VARIABLE _redlib_patch)
if(NOT _redlib_patch EQUAL 0)
    message(FATAL_ERROR "redlib-mac.patch does not apply to ${REDLIB_DIR}/include (made for psiberx/cp2077-red-lib 3fd71fe)")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REDLIB_PATCH}")

function(redmac_plugin)
    add_library(${REDMAC_NAME} SHARED ${ARGN}
        "${CMAKE_CURRENT_SOURCE_DIR}/MacSupport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/MacVault.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/MacTrap.cpp")
    set_target_properties(${REDMAC_NAME} PROPERTIES OUTPUT_NAME "${REDMAC_NAME}" PREFIX "" SUFFIX ".dylib")
    target_compile_definitions(${REDMAC_NAME} PRIVATE
        RED4EXT_PLATFORM_MACOS
        "REDMAC_TAG=\"${REDMAC_NAME}\""
        REDMAC_ENTRY=${REDMAC_ENTRY}
        REDMAC_RTTI_ID_BAND=${REDMAC_RTTI_ID_BAND}
        REDMAC_SERIAL_SLOT=${REDMAC_SERIAL_SLOT})
    # Include order matters: the generated Relocation-inl.hpp first, then this folder, then the Mac SDK, then the patched RedLib.
    # deps/RED4ext.SDK (the Windows SDK submodule) is deliberately NOT on the path.
    target_include_directories(${REDMAC_NAME} PRIVATE
        "${SDK_OVERLAY}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_INCLUDE}"
        "${REDLIB_COPY}"
        "${REDLIB_DIR}/vendor"
        "${CMAKE_CURRENT_BINARY_DIR}/Config"
        "${REDMAC_ROOT}/src")
    target_compile_options(${REDMAC_NAME} PRIVATE
        "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/MacCompat.hpp\""
        -fexceptions
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -Wno-deprecated-declarations
        -Wno-deprecated-volatile
        -Wno-unused-value
        -Wno-microsoft
        -Wno-invalid-offsetof
        -Wno-nonportable-include-path
        -Wno-extra-qualification)
    # Fail the build unless the three RED4ext entry points are exported. The ad hoc signature is for a Mac run; ios-stage.sh signs
    # the staged copy with the developer identity.
    add_custom_command(TARGET ${REDMAC_NAME} POST_BUILD
        COMMAND /usr/bin/codesign -f -s - "$<TARGET_FILE:${REDMAC_NAME}>"
        COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/check-exports.sh" "$<TARGET_FILE:${REDMAC_NAME}>" "${REDMAC_ENTRY}"
        VERBATIM)
endfunction()
