cmake_minimum_required(VERSION 3.16)
# The pinned mbedTLS release predates CMake 4.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(canokey-k230d C ASM)
set(CMAKE_C_STANDARD 11)
option(K230D_BITMANIP "Use CPU0-verified Zba/Zbb/Zbc/Zbs instructions" ON)
if(K230D_BITMANIP)
    # Override the portable toolchain baseline for every library as well as
    # the port. Keep LP64; CPU0 has no V extension and needs no FP here.
    set(k230d_arch rv64imac_zicsr_zifencei_zba_zbb_zbc_zbs)
    add_compile_options("-march=${k230d_arch}")
    add_link_options("-march=${k230d_arch}")
endif()
foreach(applet openpgp piv oath ctap admin ndef pass)
    add_link_options("-Wl,--wrap=${applet}_install")
endforeach()
add_link_options("-Wl,--wrap=ecc_sign" "-Wl,--wrap=mbedtls_rnd")
option(K230D_CACHE_ENABLED "Enable C908 caches using the K230 U-Boot settings" ON)
set(K230D_SD_CLOCK_HZ 25000000 CACHE STRING "SD1 legacy-mode clock, at most 25 MHz")
add_compile_definitions(K230D_CACHE_ENABLED=$<BOOL:${K230D_CACHE_ENABLED}>
    K230D_SD_CLOCK_HZ=${K230D_SD_CLOCK_HZ})
get_filename_component(STM32_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

option(BUILD_SDIO_BACKEND "Build the raw SDIO-sector backend and probe" ON)
if(BUILD_SDIO_BACKEND)
add_executable(k230d-sd-probe src/firmware_start.S src/sd_driver.c tests/target/sd_probe.c)
target_include_directories(k230d-sd-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(k230d-sd-probe PRIVATE -Os -g3 -Wall -Wextra -Werror -ffreestanding -fno-builtin)
target_link_options(k230d-sd-probe PRIVATE -nostdlib
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/sd_probe.ld" -Wl,--gc-sections)
set_target_properties(k230d-sd-probe PROPERTIES SUFFIX .elf
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/sd_probe.ld")
endif()

option(BUILD_CANOKEY_CORE "Build CanoKey core libraries and firmware" ON)
if(BUILD_CANOKEY_CORE)
    foreach(dep tinyusb/src/tusb.c littlefs/lfs.c tinycbor/src/cbor.h canokey-crypto/mbedtls/CMakeLists.txt)
        if(NOT EXISTS "${STM32_ROOT}/canokey-core/${dep}")
            message(FATAL_ERROR "Missing canokey-core/${dep}; initialize submodules, or use -DBUILD_CANOKEY_CORE=OFF for the SDIO probe")
        endif()
    endforeach()
    set(ENABLE_DEBUG_OUTPUT OFF CACHE BOOL "CanoKey debug output")
    set(TINYUSB_MCU OPT_MCU_NONE CACHE STRING "Out-of-tree K230D DCD")
    add_compile_definitions(TUP_DCD_ENDPOINT_MAX=8
        MBEDTLS_CONFIG_FILE="${STM32_ROOT}/Inc/mbedtls-config.h")
    add_subdirectory("${STM32_ROOT}/canokey-core" core)
    option(K230D_CRYPTO_SPEED "Optimize CPU-bound crypto for speed" ON)
    if(K230D_CRYPTO_SPEED)
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
        message(FATAL_ERROR "Pinned core fill_sn changed; review K230D identity adaptation")
    endif()
    string(REPLACE "${old_sn}"
        "extern void k230d_fill_sn(uint8_t *buf);\nvoid fill_sn(uint8_t *buf) { k230d_fill_sn(buf); }"
        admin_source "${admin_source}")
    set(old_write [=[  if (get_file_size(SN_FILE) >= 0) EXCEPT(SW_CONDITIONS_NOT_SATISFIED);
  return write_file(SN_FILE, DATA, 0, LC, 1);]=])
    string(FIND "${admin_source}" "${old_write}" write_offset)
    if(write_offset EQUAL -1)
        message(FATAL_ERROR "Pinned core admin_write_sn changed; review identity write protection")
    endif()
    string(REPLACE "${old_write}" "  EXCEPT(SW_CONDITIONS_NOT_SATISFIED);"
        admin_source "${admin_source}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/admin_k230d.c" "${admin_source}")
    # The core's temporary SN string goes out of scope before conversion.
    file(READ "${STM32_ROOT}/canokey-core/interfaces/USB/device/usb_descriptors.c" descriptors)
    string(FIND "${descriptors}" "char sn_str[9];" serial_buffer_offset)
    if(serial_buffer_offset EQUAL -1)
        message(FATAL_ERROR "Pinned USB serial buffer changed; review lifetime fix")
    endif()
    string(REPLACE "char sn_str[9];" "static char sn_str[9];" descriptors "${descriptors}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usb_descriptors_k230d.c" "${descriptors}")
    list(FILTER core_sources EXCLUDE REGEX "applets/admin/admin.c$|interfaces/USB/device/usb_descriptors.c$")
    list(APPEND core_sources "${CMAKE_CURRENT_BINARY_DIR}/admin_k230d.c"
        "${CMAKE_CURRENT_BINARY_DIR}/usb_descriptors_k230d.c")
    set_property(TARGET canokey-core PROPERTY SOURCES "${core_sources}")
    target_compile_definitions(canokey-core PRIVATE USBD_PRODUCT_STRING="CanoKey K230D Zero")

    # Keep the pinned submodule untouched; adapt its DWC2 source in the build tree.
    set(dwc2_dir "${STM32_ROOT}/canokey-core/tinyusb/src/portable/synopsys/dwc2")
    file(READ "${dwc2_dir}/dcd_dwc2.c" dcd)
    string(REPLACE "#if defined(TUP_USBIP_DWC2_STM32)"
        "#if defined(CANOKEY_K230D)\n  #include \"dwc2_k230d.h\"\n#elif defined(TUP_USBIP_DWC2_STM32)" dcd "${dcd}")
    # K230 uses its UTMI HS PHY even when running the original FS descriptors.
    string(REPLACE "return TUD_OPT_HIGH_SPEED && dwc2->ghwcfg2_bm.hs_phy_type != HS_PHY_TYPE_NONE;"
        "return dwc2->ghwcfg2_bm.hs_phy_type != HS_PHY_TYPE_NONE;" dcd "${dcd}")
    string(REPLACE "dcfg |= DCFG_DSPD_HS << DCFG_DSPD_Pos;"
        "dcfg |= DCFG_DSPD_FS_HSPHY << DCFG_DSPD_Pos;" dcd "${dcd}")
    # DWC2 4.30 uses CSFTRST_DONE rather than self-clearing CSRST.
    # Match the reset sequence in the supplied K230 SDK's dwc2.c.
    string(REPLACE "while (dwc2->grstctl & GRSTCTL_CSRST) { }"
        "k230d_wait_bits(&dwc2->grstctl, 1u << 29, true, 1000);\n  k230d_delay_ms(1);\n  dwc2->grstctl = 0;" dcd "${dcd}")
    string(REPLACE "while ( !(dwc2->grstctl & GRSTCTL_AHBIDL) ) { }"
        "k230d_wait_bits(&dwc2->grstctl, GRSTCTL_AHBIDL, true, 1000);" dcd "${dcd}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dcd_k230d.c" "${dcd}")
    add_executable(canokey-k230d src/firmware_start.S src/firmware.c src/identity.c src/ramfs.c
        "${STM32_ROOT}/Src/usb.c" "${CMAKE_CURRENT_BINARY_DIR}/dcd_k230d.c")
    target_include_directories(canokey-k230d PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${dwc2_dir}")
    target_compile_definitions(canokey-k230d PRIVATE CANOKEY_K230D TUP_USBIP_DWC2 CFG_TUSB_MCU=OPT_MCU_NONE)
    target_compile_options(canokey-k230d PRIVATE -Os -g3 -Wall -Wextra -fno-strict-aliasing)
    target_link_libraries(canokey-k230d PRIVATE canokey-core c gcc)
    target_link_options(canokey-k230d PRIVATE -nostartfiles
        "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld" -Wl,--gc-sections
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d.map")
    set_target_properties(canokey-k230d PROPERTIES SUFFIX .elf
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(canokey-k230d-image
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:canokey-k230d> ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d.bin
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_image.py
            ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d.bin ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d
        DEPENDS canokey-k230d tools/make_image.py include/raw_layout.h)
    if(BUILD_SDIO_BACKEND)
    add_executable(canokey-k230d-sd src/firmware_start.S src/firmware.c src/identity.c src/sd_fs.c src/sd_driver.c
        "${STM32_ROOT}/Src/usb.c" "${CMAKE_CURRENT_BINARY_DIR}/dcd_k230d.c")
    target_include_directories(canokey-k230d-sd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${dwc2_dir}")
    target_compile_definitions(canokey-k230d-sd PRIVATE CANOKEY_K230D CANOKEY_STORAGE_SD K230D_SD_DRIVER
        TUP_USBIP_DWC2 CFG_TUSB_MCU=OPT_MCU_NONE)
    target_compile_options(canokey-k230d-sd PRIVATE -Os -g3 -Wall -Wextra -fno-strict-aliasing)
    target_link_libraries(canokey-k230d-sd PRIVATE canokey-core c gcc)
    target_link_options(canokey-k230d-sd PRIVATE -nostartfiles
        "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld" -Wl,--gc-sections
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d-sd.map")
    set_target_properties(canokey-k230d-sd PROPERTIES SUFFIX .elf
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/firmware.ld")
    add_custom_target(canokey-k230d-sd-image
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:canokey-k230d-sd> ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d-sd.bin
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_image.py
            ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d-sd.bin ${CMAKE_CURRENT_BINARY_DIR}/canokey-k230d-sd --with-storage
        DEPENDS canokey-k230d-sd tools/make_image.py include/raw_layout.h)
    endif()
endif()
