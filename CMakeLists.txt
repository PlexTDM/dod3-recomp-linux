cmake_minimum_required(VERSION 3.20)
project(dod3
    VERSION 0.4.0
    LANGUAGES C CXX
    DESCRIPTION "Drakengard 3 (PS3) recompiled for Linux"
)
# The port's own version, apart from the game's (the 1.01 update).
set(DOD3_VERSION_LABEL "0.4.0 alpha")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "dod3-recomp currently supports Linux builds only")
endif()

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# The Linux installer alone (dod3-setup)
# ---------------------------------------------------------------------------
# Optional Linux installer-only target; the native game target is the default.
option(DOD3_SETUP_ONLY "Build only the Linux installer (dod3-setup)" OFF)
if(DOD3_SETUP_ONLY)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    if(NOT DEFINED PS3RECOMP_DIR)
        set(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ps3recomp")
    endif()
    get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)
    if(NOT EXISTS "${PS3RECOMP_DIR}/tools/pkg_extract.py")
        message(FATAL_ERROR "ps3recomp is not checked out. Run: git submodule update --init")
    endif()

    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

    # The retail PKG key, from the submodule's tools/pkg_extract.py.
    set(_pkg_py "${PS3RECOMP_DIR}/tools/pkg_extract.py")
    file(STRINGS "${_pkg_py}" _pkgkey_line REGEX "^PS3_GPKG_KEY *= *bytes\\.fromhex")
    string(REGEX MATCH "\"([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])\"" _m "${_pkgkey_line}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "${_pkg_py}: no PS3_GPKG_KEY")
    endif()
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," _pkgkey_c "${CMAKE_MATCH_1}")
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/ps3recomp_pkg_key.h" CONTENT
"/* Generated from ps3recomp/tools/pkg_extract.py (PS3_GPKG_KEY); not in git. */
static const unsigned char k_ps3recomp_pkg_key[16] = {${_pkgkey_c}};
")

    # The installer's typeface, as arrays in gen/setup_fonts.h
    set(_font_h "/* Generated from third_party/fonts/inter by CMakeLists.txt. */\n")
    foreach(_font Inter-Regular Inter-Medium Inter-SemiBold InterDisplay-Bold)
        set(_ttf "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/${_font}.ttf")
        file(READ "${_ttf}" _hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
        string(MAKE_C_IDENTIFIER "${_font}" _id)
        string(APPEND _font_h "static const unsigned char kFont_${_id}[] = {${_bytes}};\n")
    endforeach()
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/setup_fonts.h" CONTENT "${_font_h}" @ONLY)

    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/app_version.h" CONTENT
"/* Generated from CMakeLists.txt: the port's own version. */
#define DOD3_VER_MAJOR ${PROJECT_VERSION_MAJOR}
#define DOD3_VER_MINOR ${PROJECT_VERSION_MINOR}
#define DOD3_VER_PATCH ${PROJECT_VERSION_PATCH}
#define DOD3_VERSION_STRING \"${DOD3_VERSION_LABEL}\"
")

    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(DOD3_SDL2 SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(DOD3_SDL2 SDL2::SDL2-static)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(DOD3_SDL2 PkgConfig::SDL2)
    endif()

    set(SETUP_CORE
        src/install/install.cpp
        src/install/iso.cpp
        src/install/pkg.cpp
        src/install/zip.cpp
        src/install/crypto.cpp
        src/install/self.cpp
        src/install/keys.cpp
        src/kernel/dlc.cpp
        src/util.cpp)
    set(SETUP_UI
        src/ui/wizard.cpp
        src/ui/look.cpp
        src/ui/setup_shared.cpp
        src/ui/png.cpp
        src/ui/ui_linux.cpp)
    set(IMGUI_SOURCES
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")

    add_executable(dod3-setup
        src/os/linux/setup_linux.cpp
        ${SETUP_CORE}
        ${SETUP_UI}
        ${IMGUI_SOURCES})

    target_include_directories(dod3-setup PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${IMGUI_DIR}"
        "${CMAKE_BINARY_DIR}/gen"
        "${PS3RECOMP_DIR}/libs/codec")
    target_compile_features(dod3-setup PRIVATE cxx_std_20)
    target_compile_definitions(dod3-setup PRIVATE DOD3_EBOOT=101)
    target_compile_options(dod3-setup PRIVATE
        -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(src/ui/png.cpp PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(src/install/iso.cpp PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
    target_link_libraries(dod3-setup PRIVATE
        ${DOD3_SDL2}
        OpenGL::GL
        ZLIB::ZLIB
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m)
    install(TARGETS dod3-setup RUNTIME DESTINATION bin)

    message(STATUS "dod3-setup:      Linux installer only (no lifted game code)")
    message(STATUS "Install folder:  --base DIR, DOD3_INSTALL_BASE, or ~/.local/share/Drakengard 3 Recompiled")
    return()
endif()

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    set(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ps3recomp")
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

file(STRINGS "${PS3RECOMP_DIR}/libs/spurs/spurs_taskset.c"
     _spurs_tasksets_on REGEX "int spurs_tasksets_on\\(")
if(NOT _spurs_tasksets_on)
    find_package(Git REQUIRED)
    set(_spurs_compat_patch
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/sp00nznet-spurs-taskset-registry.patch")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PS3RECOMP_DIR}" apply --unidiff-zero --check "${_spurs_compat_patch}"
        RESULT_VARIABLE _spurs_patch_check
        OUTPUT_QUIET ERROR_VARIABLE _spurs_patch_error)
    if(NOT _spurs_patch_check EQUAL 0)
        message(FATAL_ERROR
            "The selected ps3recomp runtime lacks the SPURS taskset registry used by "
            "Drakengard 3, and the compatibility patch cannot be applied:\n${_spurs_patch_error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PS3RECOMP_DIR}" apply --unidiff-zero "${_spurs_compat_patch}"
        RESULT_VARIABLE _spurs_patch_result
        ERROR_VARIABLE _spurs_patch_error)
    if(NOT _spurs_patch_result EQUAL 0)
        message(FATAL_ERROR "Could not apply the SPURS runtime compatibility patch:\n${_spurs_patch_error}")
    endif()
endif()

option(PS3RECOMP_RSX_VULKAN "Use the runtime's Vulkan renderer" ON)

# The EBOOT the lift is from: 101 (the 1.01 update, lifted into
# recompiled_101/; the default, and the version the port is built and tested
# for) or 100 (BLUS31197 1.00, the disc, recompiled/; kept for comparison,
# not maintained). src/cpu/eboot.h holds every address the port's own code
# knows, per version. A 1.00 build sits beside the 1.01 one:
#   cmake -B build_100 -G Ninja -DDOD3_EBOOT=100
set(DOD3_EBOOT 101 CACHE STRING "EBOOT version the lift is from: 101 or 100")
set_property(CACHE DOD3_EBOOT PROPERTY STRINGS 100 101)
if(DOD3_EBOOT STREQUAL "101")
    set(_dod3_recomp_default "${CMAKE_CURRENT_SOURCE_DIR}/recompiled_101")
else()
    set(_dod3_recomp_default "${CMAKE_CURRENT_SOURCE_DIR}/recompiled")
endif()
set(RECOMP_DIR "${_dod3_recomp_default}" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()
# The lift must be the version's. A build folder keeps RECOMP_DIR in its
# cache, so one configured for the other version would build 1.01 code
# against the 1.00 lift and fail at link; the hooked functions' lifted
# bodies tell the two apart.
if(DOD3_EBOOT STREQUAL "101")
    set(_dod3_lift_mark "func_00EE7728_lifted")
else()
    set(_dod3_lift_mark "func_00EE6538_lifted")
endif()
file(STRINGS "${RECOMP_DIR}/ppu_recomp.h" _dod3_lift_hit REGEX "${_dod3_lift_mark}")
if(NOT _dod3_lift_hit)
    message(FATAL_ERROR
        "${RECOMP_DIR} is not a ${DOD3_EBOOT} lift: no ${_dod3_lift_mark} in its ppu_recomp.h. "
        "Pass -DRECOMP_DIR=<the ${DOD3_EBOOT} lift>, or configure a fresh build folder.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Build the Linux runtime in-tree so its SDL2, Vulkan and threading dependencies
# propagate to the game target.
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)
find_package(SDL2 REQUIRED)
if(PS3RECOMP_RSX_VULKAN)
    find_package(glslang CONFIG QUIET)
endif()
if(PS3RECOMP_RSX_VULKAN AND
   (NOT TARGET glslang::glslang OR NOT TARGET glslang::SPIRV))
    message(FATAL_ERROR
        "The Linux game renderer needs glslang to translate the game's shaders. "
        "Install the glslang development package or configure with "
        "-DPS3RECOMP_RSX_VULKAN=OFF for a headless build.")
endif()

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/patches/spu_hooks.c"     # --native-hook targets
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/msdsp_hooks.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/mp3_native.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/mp3dec.c")
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c spu_overlays.c jobmods_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Parts of libsre (SPURS LFQueue push, SPURS queue), lifted at build time
# ---------------------------------------------------------------------------
# src/kernel/spurs_lfqueue.cpp and src/kernel/spurs_queue.cpp drive Sony's own code out of
# libsre.prx; tools/gen_libsre.py lifts it into the build tree. The result is
# firmware-derived, so it is generated, never committed.
set(LIBSRE_PRX "${PS3RECOMP_DIR}/fw_spu/libsre.prx" CACHE FILEPATH
    "Decrypted libsre.prx the SPURS queue paths are lifted from")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
    set(DOD3_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(DOD3_PYTHON "${Python3_EXECUTABLE}")
endif()
set(LIBSRE_LIFTED_CPP "${CMAKE_CURRENT_BINARY_DIR}/gen/libsre_lifted.cpp")
add_custom_command(
    OUTPUT "${LIBSRE_LIFTED_CPP}"
    COMMAND "${DOD3_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_libsre.py"
            "${PS3RECOMP_DIR}" "${LIBSRE_PRX}"
            "${CMAKE_CURRENT_BINARY_DIR}/libsre" "${LIBSRE_LIFTED_CPP}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_libsre.py"
            "${LIBSRE_PRX}"
            "${PS3RECOMP_DIR}/tools/ppu_lifter.py"
            "${PS3RECOMP_DIR}/tools/lift_prx.py"
    COMMENT "Lifting libsre's SPURS queue paths")
set_source_files_properties("${LIBSRE_LIFTED_CPP}" PROPERTIES COMPILE_OPTIONS "-w;-ffp-contract=off")

list(APPEND PORT_PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input/rsx_host_mouse_linux.cpp")

# The installer (src/install/install.h): the core on every platform; the pages
# (src/ui/wizard.cpp) in Dear ImGui (third_party/imgui, v1.92.9b), in a
# window from one layer per platform (src/ui/ui.h).
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(SETUP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/install.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/pkg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/zip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/crypto.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/self.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/install/keys.cpp")
# The retail PKG key, read from the submodule's tools/pkg_extract.py
# (PS3_GPKG_KEY) into a header in the build tree: this repository carries no
# copy, and the key stays off the compile command lines and build logs.
set(_pkg_py "${PS3RECOMP_DIR}/tools/pkg_extract.py")
file(STRINGS "${_pkg_py}" _pkgkey_line REGEX "^PS3_GPKG_KEY *= *bytes\\.fromhex")
string(REGEX MATCH "\"([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])\"" _m "${_pkgkey_line}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "${_pkg_py}: no PS3_GPKG_KEY -- the installer reads the retail PKG key from there")
endif()
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," _pkgkey_c "${CMAKE_MATCH_1}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/ps3recomp_pkg_key.h" CONTENT
"/* Generated from ps3recomp/tools/pkg_extract.py (PS3_GPKG_KEY); not in git. */
static const unsigned char k_ps3recomp_pkg_key[16] = {${_pkgkey_c}};
")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pkg_py}")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/install/pkg.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/gen")
set_property(SOURCE ${SETUP_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${IMGUI_DIR}")

# The port's own sources, grouped by module under src/.
set(PORT_CPU_SOURCES                 # the lifted code's glue
    src/cpu/hot.cpp)
set(PORT_INPUT_SOURCES               # the camera's stick; the keyboard and mouse as the pad
    src/input/camera.cpp
    src/input/kbm.cpp)
set(PORT_KERNEL_SOURCES              # the port's own HLE
    src/kernel/hle.cpp
    src/kernel/spurs_lfqueue.cpp
    src/kernel/spurs_queue.cpp
    src/kernel/dlc.cpp
    src/kernel/mods.cpp)             # mods' files and the DLC chapters' voices, through the VFS's hook
set(PORT_PATCH_SOURCES               # native replacements of the game's own code
    src/patches/edgezlib.cpp
    src/patches/gc.cpp
    src/patches/gc_native.cpp
    src/patches/load_idle.cpp        # the async loader in the frame limiter's sleep
    src/patches/collision_native.cpp # generated by tools/lift_native.py
    src/patches/menu_patch.cpp
    src/patches/prompts.cpp          # the button prompts in the pad's own glyphs
    src/patches/hud_aspect.cpp       # the HUD in a view wider or narrower than 16:9
    src/patches/sysset.cpp)
set(PORT_UI_SOURCES                  # the in-game settings pages
    src/ui/settings_menu.cpp
    src/ui/ini.cpp)                  # dod3.ini, edited in place
set(PORT_SOURCES
    src/main.cpp
    src/util.cpp
    src/install/iso.cpp              # the disc reader, which the game uses too
    ${PORT_CPU_SOURCES}
    ${PORT_INPUT_SOURCES}
    ${PORT_KERNEL_SOURCES}
    ${PORT_PATCH_SOURCES}
    ${PORT_UI_SOURCES}
)

add_executable(${PROJECT_NAME}
    ${PORT_SOURCES}
    ${PORT_PLATFORM_SOURCES}
    ${SETUP_SOURCES}
    ${LIBSRE_LIFTED_CPP}
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)
if(EXISTS "${PS3RECOMP_DIR}/include/ps3emu/vm_watch.h")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DOD3_RUNTIME_VM_WATCH)
endif()

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2;-msse4.1;-mfma")
    else()
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
endif()

# The port's sources include one another by module path ("install/pkg.h",
# "cpu/eboot.h"): src/ is their include root. Scoped to them, as the SPU
# include directory is above, so the lifted TUs' compile commands stay as
# they are.
set_property(SOURCE ${PORT_SOURCES} ${PORT_PLATFORM_SOURCES} ${SETUP_SOURCES}
             "${CMAKE_CURRENT_SOURCE_DIR}/src/patches/spu_hooks.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/msdsp_hooks.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/mp3_native.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/apu/mp3dec.c"
             APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Copy the default settings file into a fresh build directory only.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/dod3.ini")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/dod3.ini" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# The port's version as a header for main.cpp's banner.
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/app_version.h" CONTENT
"/* Generated from CMakeLists.txt: the port's own version. */
#define DOD3_VER_MAJOR ${PROJECT_VERSION_MAJOR}
#define DOD3_VER_MINOR ${PROJECT_VERSION_MINOR}
#define DOD3_VER_PATCH ${PROJECT_VERSION_PATCH}
#define DOD3_VERSION_STRING \"${DOD3_VERSION_LABEL}\"
")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/gen")

# The button-prompt atlases (src/res/prompts, drawn by tools/prompt_atlas.py)
# as arrays in gen/prompts_data.h, for src/patches/prompts.cpp: the pads',
# and the sheet of key caps the keyboard's is made from.
set(_prompt_h "/* Generated from src/res/prompts by CMakeLists.txt. */\n")
set(_prompt_sets ps4 ps5 xbox switch keys)
foreach(_set IN LISTS _prompt_sets)
    set(_png "${CMAKE_CURRENT_SOURCE_DIR}/src/res/prompts/${_set}.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_png}")
    file(READ "${_png}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    string(APPEND _prompt_h "static const unsigned char kPromptPng_${_set}[] = {${_bytes}};\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/gen/prompts_data.h" CONTENT "${_prompt_h}" @ONLY)

target_compile_definitions(${PROJECT_NAME} PRIVATE DOD3_EBOOT=${DOD3_EBOOT})
message(STATUS "EBOOT:           BLUS31197 ${DOD3_EBOOT}")

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)
# Let CMake infer the C++ linker for the runtime and generated C sources.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fno-strict-aliasing)

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - on x86-64, DOD3_X86_LEVEL (default x86-64-v3: AVX2, FMA,
#     BMI, LZCNT, MOVBE -- any Intel since Haswell, any AMD since Excavator).
#     At the baseline level every lifted fmadd is a call into the C runtime's
#     fma(), sqrt() and trunc() are calls as well, cntlzw is a bsr with a
#     branch around it, and the AltiVec byte shuffles have no pshufb. With the
#     level raised contraction has to be switched off by name, as it is on
#     arm64, and -fno-math-errno lets sqrt be the instruction. main.cpp checks
#     the CPU at start (DOD3_X86_V3). Set DOD3_X86_LEVEL to an empty string
#     for a build that runs on anything.
#   - no on-demand guards on thread-local reads. The lifted code
#     reads one thread-local, the tail-call trampoline, after every call that
#     may leave it set; as a C++ extern thread-local each read first tests
#     whether the module's dynamic TLS initialisers have run on this thread,
#     which an executable's always have. The check and its call were 20 of
#     the 43 instructions of a call-and-drain.
set(_recomp_opts "-w;-ffp-contract=off")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(DOD3_X86_LEVEL "x86-64-v3" CACHE STRING
        "-march for the lifted PPU code on x86-64 (empty: baseline x86-64)")
    if(DOD3_X86_LEVEL)
        list(APPEND _recomp_opts "-march=${DOD3_X86_LEVEL}" "-fno-math-errno")
        if(DOD3_X86_LEVEL MATCHES "v3|v4|haswell|skylake|znver")
            set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS DOD3_X86_V3)
        endif()
    endif()
endif()
set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${_recomp_opts}")
# The native collision test (src/patches/collision_native.cpp, generated from
# the lifted body) must round exactly as the lifted code does: the same
# x86 level (a fused fma instruction, which flushes denormals as the thread's
# MXCSR says, where the C runtime's fma() does not), no contraction, no errno.
set_property(SOURCE src/patches/collision_native.cpp APPEND PROPERTY COMPILE_OPTIONS "-ffp-contract=off")
# PPU_INLINE_VM: the lifted code's loads and stores inline the runtime's fast
# path (ps3recomp/runtime/ppu/ppu_vm_fast.h) instead of calling vm_read*/
# vm_write*. The render thread spent ~60% of its time in those calls.
option(DOD3_INLINE_VM "Inline the guest-memory fast path into the lifted code" ON)
if(DOD3_INLINE_VM)
    set_property(SOURCE ${RECOMP_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS PPU_INLINE_VM)
endif()
set_property(SOURCE ${RECOMP_SOURCES} APPEND PROPERTY INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/ppu")

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
