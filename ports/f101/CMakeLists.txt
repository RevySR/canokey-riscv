cmake_minimum_required(VERSION 3.19)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(canokey-f101 C ASM)
set(CMAKE_C_STANDARD 11)
add_compile_definitions(MBEDTLS_CTR_DRBG_C)
foreach(applet openpgp piv oath ctap admin ndef pass)
    add_link_options("-Wl,--wrap=${applet}_install")
endforeach()
add_link_options("-Wl,--wrap=mbedtls_rnd")
get_filename_component(STM32_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Keep assertion paths independent of the checkout location.
add_compile_options("-ffile-prefix-map=${STM32_ROOT}=.")

add_executable(f101-psram-init src/boot/fel_start.S
    src/boot/fel.c src/psram.c)
target_include_directories(f101-psram-init PRIVATE include)
target_compile_options(f101-psram-init PRIVATE -Os -g3 -Wall -Wextra -Werror -ffreestanding)
target_link_libraries(f101-psram-init PRIVATE gcc)
target_link_options(f101-psram-init PRIVATE -nostdlib -Wl,--gc-sections
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/psram_fel.ld")
set_target_properties(f101-psram-init PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/psram_fel.ld")
add_custom_command(TARGET f101-psram-init POST_BUILD COMMAND ${CMAKE_OBJCOPY} -O binary
    $<TARGET_FILE:f101-psram-init> ${CMAKE_CURRENT_BINARY_DIR}/f101-psram-init.bin)

add_executable(f101-spl EXCLUDE_FROM_ALL src/boot/start.S src/boot/boot.c
    src/boot/platform.c src/spi_nor.c src/psram.c)
target_include_directories(f101-spl PRIVATE include)
target_compile_options(f101-spl PRIVATE -Os -g3 -Wall -Wextra -Werror -ffreestanding)
target_link_libraries(f101-spl PRIVATE gcc)
target_link_options(f101-spl PRIVATE -nostdlib -Wl,--gc-sections
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/boot.ld")
set_target_properties(f101-spl PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/boot.ld")
add_custom_command(TARGET f101-spl POST_BUILD COMMAND ${CMAKE_OBJCOPY} -O binary
    $<TARGET_FILE:f101-spl> ${CMAKE_CURRENT_BINARY_DIR}/f101-spl.bin)

add_executable(f101-nor-install tools/target/fel_start.S tools/target/nor_install.c
    src/spi_nor.c src/boot/platform.c)
target_include_directories(f101-nor-install PRIVATE include)
target_compile_definitions(f101-nor-install PRIVATE F101_NOR_INSTALL)
target_compile_options(f101-nor-install PRIVATE -Os -g3 -Wall -Wextra -Werror -ffreestanding)
target_link_options(f101-nor-install PRIVATE -nostdlib -Wl,--gc-sections
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/fel.ld")
set_target_properties(f101-nor-install PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/fel.ld")
add_custom_command(TARGET f101-nor-install POST_BUILD COMMAND ${CMAKE_OBJCOPY} -O binary
    $<TARGET_FILE:f101-nor-install> ${CMAKE_CURRENT_BINARY_DIR}/f101-nor-install.bin)
add_dependencies(f101-nor-install f101-psram-init)

add_executable(f101-nor-read EXCLUDE_FROM_ALL tools/target/fel_start.S
    tools/target/nor_read.c src/spi_nor.c)
target_include_directories(f101-nor-read PRIVATE include)
target_compile_options(f101-nor-read PRIVATE -Os -g3 -Wall -Wextra -Werror -ffreestanding)
target_link_options(f101-nor-read PRIVATE -nostdlib -Wl,--gc-sections
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/fel.ld")
set_target_properties(f101-nor-read PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/fel.ld")
add_custom_command(TARGET f101-nor-read POST_BUILD COMMAND ${CMAKE_OBJCOPY} -O binary
    $<TARGET_FILE:f101-nor-read> ${CMAKE_CURRENT_BINARY_DIR}/f101-nor-read.bin)
add_dependencies(f101-nor-read f101-psram-init)

option(BUILD_CANOKEY_CORE "Build CanoKey core libraries and firmware" ON)
if(BUILD_CANOKEY_CORE)
    option(F101_BITMANIP "Use Zba/Zbb/Zbc/Zbs integer extensions" ON)
    if(F101_BITMANIP)
        set(f101_arch rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs)
        add_compile_options("-march=${f101_arch}")
        add_link_options("-march=${f101_arch}")
    endif()
    foreach(dep tinyusb/src/tusb.c littlefs/lfs.c tinycbor/src/cbor.h canokey-crypto/mbedtls/CMakeLists.txt)
        if(NOT EXISTS "${STM32_ROOT}/canokey-core/${dep}")
            message(FATAL_ERROR "Missing canokey-core/${dep}; initialize submodules")
        endif()
    endforeach()
    set(ENABLE_DEBUG_OUTPUT OFF CACHE BOOL "CanoKey debug output")
    set(TINYUSB_MCU OPT_MCU_NONE CACHE STRING "Out-of-tree F101 DCD")
    add_compile_definitions(TUP_DCD_ENDPOINT_MAX=8
        MBEDTLS_CONFIG_FILE="${STM32_ROOT}/Inc/mbedtls-config.h")
    add_subdirectory("${STM32_ROOT}/canokey-core" core)
    option(F101_CRYPTO_SPEED "Optimize CPU-bound crypto for speed" ON)
    if(F101_CRYPTO_SPEED)
        target_compile_options(mbedcrypto PRIVATE -O2)
        target_compile_options(canokey-crypto PRIVATE -O2)
    endif()
    # Do not link TinyUSB's OPT_MCU_NONE dummy driver.
    get_target_property(core_sources canokey-core SOURCES)
    list(FILTER core_sources EXCLUDE REGEX "portable/template/dcd_template.c$")
    # Board identity is immutable and independent of removable storage.
    # Adapt the pinned core in the build tree, including its internal SN calls.
    file(READ "${STM32_ROOT}/canokey-core/applets/admin/admin.c" admin_source)
    set(old_sn [=[void fill_sn(uint8_t *buf) {
  const int err = read_file(SN_FILE, buf, 0, 4);
  if (err != 4) memset(buf, 0, 4);
}]=])
    string(FIND "${admin_source}" "${old_sn}" sn_offset)
    if(sn_offset EQUAL -1)
        message(FATAL_ERROR "Pinned core fill_sn changed; review F101 identity adaptation")
    endif()
    string(REPLACE "${old_sn}"
        "extern void f101_fill_sn(uint8_t *buf);\nvoid fill_sn(uint8_t *buf) { f101_fill_sn(buf); }"
        admin_source "${admin_source}")
    set(old_write [=[  if (get_file_size(SN_FILE) >= 0) EXCEPT(SW_CONDITIONS_NOT_SATISFIED);
  return write_file(SN_FILE, DATA, 0, LC, 1);]=])
    string(FIND "${admin_source}" "${old_write}" write_offset)
    if(write_offset EQUAL -1)
        message(FATAL_ERROR "Pinned core admin_write_sn changed; review identity write protection")
    endif()
    string(REPLACE "${old_write}" "  EXCEPT(SW_CONDITIONS_NOT_SATISFIED);"
        admin_source "${admin_source}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/admin_f101.c" "${admin_source}")
    # The core's temporary SN string goes out of scope before conversion.
    file(READ "${STM32_ROOT}/canokey-core/interfaces/USB/device/usb_descriptors.c" descriptors)
    string(FIND "${descriptors}" "char sn_str[9];" serial_buffer_offset)
    if(serial_buffer_offset EQUAL -1)
        message(FATAL_ERROR "Pinned USB serial buffer changed; review lifetime fix")
    endif()
    string(REPLACE "char sn_str[9];" "static char sn_str[9];" descriptors "${descriptors}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usb_descriptors_f101.c" "${descriptors}")
    list(FILTER core_sources EXCLUDE REGEX "applets/admin/admin.c$|interfaces/USB/device/usb_descriptors.c$")
    list(APPEND core_sources "${CMAKE_CURRENT_BINARY_DIR}/admin_f101.c"
        "${CMAKE_CURRENT_BINARY_DIR}/usb_descriptors_f101.c")
    set_property(TARGET canokey-core PROPERTY SOURCES "${core_sources}")
    target_compile_definitions(canokey-core PRIVATE USBD_PRODUCT_STRING="CanoKey F101 YuzukiNeko")


    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tools/adapt_musb.py)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/adapt_musb.py
        ${STM32_ROOT}/canokey-core/tinyusb/src/portable/sunxi ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY)
    option(F101_STORAGE_NOR "Store credentials in SPI NOR" ON)
    if(F101_STORAGE_NOR)
        set(storage_sources src/nor_fs.c src/spi_nor.c src/seed_journal.c)
    else()
        set(storage_sources src/ramfs.c)
    endif()
    add_executable(canokey-f101 src/firmware_start.S src/firmware.c src/identity.c ${storage_sources} src/platform.c
        "${STM32_ROOT}/Src/usb.c" "${CMAKE_CURRENT_BINARY_DIR}/dcd_f101.c")
    target_include_directories(canokey-f101 PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(canokey-f101 PRIVATE CANOKEY_F101 F101_STORAGE_NOR=$<BOOL:${F101_STORAGE_NOR}> CFG_TUSB_MCU=OPT_MCU_NONE)
    target_compile_options(canokey-f101 PRIVATE -Os -g3 -Wall -Wextra -fno-strict-aliasing)
    target_link_libraries(canokey-f101 PRIVATE canokey-core c gcc)
    target_link_options(canokey-f101 PRIVATE -nostartfiles "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld"
        -Wl,--gc-sections "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/canokey-f101.map")
    set_target_properties(canokey-f101 PROPERTIES SUFFIX .elf LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld")
    add_custom_command(TARGET canokey-f101 POST_BUILD COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:canokey-f101> ${CMAKE_CURRENT_BINARY_DIR}/canokey-f101.bin)
    add_dependencies(canokey-f101 f101-psram-init)
    if(F101_STORAGE_NOR)
        add_custom_target(canokey-f101-spi-image ALL
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_boot_image.py
                --spl ${CMAKE_CURRENT_BINARY_DIR}/f101-spl.bin
                --firmware ${CMAKE_CURRENT_BINARY_DIR}/canokey-f101.bin
                --output ${CMAKE_CURRENT_BINARY_DIR}/canokey-f101-spi.img
            DEPENDS canokey-f101 f101-spl
            BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/canokey-f101-spi.img
            VERBATIM)
    endif()
endif()
