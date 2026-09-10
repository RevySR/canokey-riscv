# CanoKey F101S3 / YuzukiNeko 移植

本目录为 Allwinner F101S3 的 RV32 裸机移植，运行在 YuzukiNeko 板卡上。
固件启用 C907 指令和数据缓存，在 16 MiB type-3 SiP PSRAM 中运行，
通过 SPI NOR 保存凭据，支持直接从 SPI NOR 启动，也支持通过 FEL 加载调试。

USB 提供 FIDO2/U2F、CCID、WebUSB 和 CanoKey 键盘接口，使用全速
12 Mbps、FIFO PIO 和轮询方式。设备序列号由芯片公开的 128 位 SID 派生。
默认通过 CE 加速 AES，并用于 CanoKey 的 AES 操作和 mbedTLS CTR_DRBG。
CE 从外设 PLL 分频到不超过 100 MHz；外设 PLL 不可用时使用 24 MHz 时钟。
可用 `-DF101_CE_AES=OFF` 构建纯软件 AES 版本。
应用和密码库默认启用 `Zba/Zbb/Zbc/Zbs`，ABI 保持 ILP32；
`-DF101_BITMANIP=OFF` 可切回基础指令集。SPL 和 FEL 烧录工具保持基础指令集。

## 依赖

- CMake 3.19 或更新版本、Python 3。
- 支持 RV32/ILP32 的 `riscv64-unknown-elf` GCC 和 C 库；本移植使用 picolibc。
- 已初始化的仓库子模块。
- FEL 烧录、备份和调试需要支持 F101 的 xfel 主机工具。

以下命令均从 `canokey-stm32` 仓库根目录执行。

## 构建及产物

```sh
git submodule update --init --recursive
cmake -S ports/f101 -B ports/f101/build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake
cmake --build ports/f101/build -j
```

默认构建使用 SPI NOR 存储，并自动生成完整的 SPI 启动镜像：

| 产物 | 用途 |
| --- | --- |
| `ports/f101/build/canokey-f101-spi.img` | 完整 SPI 启动镜像，包含 SPL 和应用 |
| `ports/f101/build/canokey-f101.bin` | 用于 FEL 加载的应用裸固件 |
| `ports/f101/build/canokey-f101.elf` | 带调试符号的应用固件 |

PSRAM 初始化代码已链接进 SPL，完整 SPI 镜像包含启动所需的 SPL 和应用。
CMake 负责固件和 FEL 加载、备份、烧录辅助程序的编译，脚本从输入文件所在的
构建目录读取这些程序。运行工具时请保留同一构建目录中的配套产物。

交叉编译器前缀通过 `-DCROSS_COMPILE=<prefix>` 设置。

使用 RAM 文件系统时，配置 `-DF101_STORAGE_NOR=OFF`。该版本的
凭据和配置在复位后丢失，不自动生成 SPI 启动镜像。

## 首次备份与 SPI 烧录

先让板卡进入 FEL 模式，备份完整的 16 MiB NOR：

```sh
cmake --build ports/f101/build --target f101-nor-read
python3 ports/f101/tools/read_nor.py ports/f101/build/f101-nor-read.bin \
  nor-backup.bin --length 0x1000000
```

备份工具只执行 Flash 读取，拒绝覆盖已有输出文件。备份应保存在构建目录
之外，并妥善保管，因为使用后的 NOR 中可能包含凭据和随机数状态。

```sh
python3 ports/f101/tools/install_boot.py \
  ports/f101/build/canokey-f101-spi.img --backup nor-backup.bin
```

安装器在访问设备前校验 SPL 校验和、应用头、长度和 CRC，并在写入 SRAM
前确认芯片为 F101。随后备份即将覆盖的启动槽，只更新变化的扇区，
并逐扇区回读验证。
它先写应用、最后写入 SPL 头部，保留凭据区和已有随机数状态。只有两个
随机数状态扇区都为空白时，才会从主机操作系统生成新种子并初始化它们。
首次安装使用上述安装器，同时完成镜像写入和随机数状态初始化。

`xfel spinor write` 会先擦除写入范围覆盖的扇区，再编程文件内容。种子日志
需要先写记录内容，再单独写入同一扇区中的提交标记；若直接换成两次
`xfel spinor write`，第二次调用会擦掉第一次写入的记录。因此这里通过
`tools/target/` 中的板端程序分别执行擦除和无擦除编程，主机脚本使用 FEL
传输命令和数据。该限制来自原生写命令的自动擦除语义，不代表 xfel 无法
识别这块 NOR。

烧录完成后，不按 FEL 键，正常复位即可从 SPI NOR 启动。固件更新不具备
掉电原子性；如果烧录中断，可强制进入 FEL 恢复。

## NOR 布局与文件系统

| NOR 地址范围 | 用途 |
| --- | --- |
| `0x000000–0x00FFFF` | 带 eGON 头的 SPL |
| `0x010000–0x07FFFF` | 应用头、CRC32 和固件 |
| `0x080000–0xFBDFFF` | 保留原内容 / 预留 |
| `0xFBE000–0xFBFFFF` | 两个启动随机数状态扇区 |
| `0xFC0000–0xFFFFFF` | 256 KiB CanoKey littlefs |

SPI0 使用 12 MHz 单线传输、256 字节页编程和 4 KiB 扇区擦除。
支持写入的 Flash JEDEC ID 为 `85 20 18`，保留原有 Flash 保护设置。
运行时驱动只能写随机数状态区和 littlefs，独立的 FEL 安装器才允许写启动槽。

文件系统只有在整个区域完全擦空时才会自动格式化。已有数据挂载失败会
停止启动。编程和擦除均进行回读校验。

SPL 初始化 PSRAM，将应用加载到 `0x40010000`，检查长度和 CRC32 后跳转。
CRC32 用于检测数据损坏，不提供固件身份认证或安全启动。

## FEL 调试与串口

UART1 使用 PB0 TX / PB1 RX，参数为 115200 8N1。进入 FEL 后可直接加载应用：

```sh
python3 ports/f101/tools/load_firmware.py ports/f101/build/canokey-f101.bin
```

加载器检查芯片 ID、初始化 PSRAM、写入并回读应用，提供一次性随机
种子后启动。NOR 版应用将配置和凭据保存到 Flash。再次加载需重新进入 FEL。

默认构建不输出串口信息，包括 SPL、应用启动及异常处理。
需要日志时，使用独立的 Debug 构建目录：

```sh
cmake -S ports/f101 -B ports/f101/build-debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build ports/f101/build-debug -j
```

Debug 版本输出启动进度和异常信息。串口确认输入在两种构建中均可使用：

| 输入 | 功能 |
| --- | --- |
| `t` | 注入短按确认 |
| `l` | 注入长按确认 |

目前未实现物理确认 GPIO，串口确认仅用于开发测试，不能作为产品的物理
用户在场机制。

## 随机数状态

FEL 加载时，主机在 `0x40FFF004` 提供 48 字节新随机数据，并在
`0x40FFF000` 放置标记。固件使用后擦除这块内存，以初始化 mbedTLS CTR_DRBG。

SPI 启动则读取独立的双扇区状态日志。最新有效状态必须先持久化标记为
已使用，固件随后初始化 DRBG，生成后继状态，并将它提交到另一扇区，
之后才处理应用请求。记录包含 SHA-256 完整性校验和最后写入的提交标记。

更新中断或日志损坏时停止启动。进入 FEL 后，可显式重建随机数日志：

```sh
python3 ports/f101/tools/install_boot.py \
  ports/f101/build/canokey-f101-spi.img --backup nor-backup.bin --recover-rng
```

此操作将旧日志保存到备份目录的 `seed-journal-before-*.bin`，使用主机新熵
重建两个状态扇区，并按所选镜像更新固件，保留凭据。完成后正常复位即可。
普通安装不会替换已有日志；单次 FEL 加载也不会修复日志。

该方案依赖主机初始化的随机种子，不具备硬件真随机源、持续熵补充或 NOR
快照回滚保护。DRBG 达到重播种限制时停止运行。

## 源码目录

| 目录 | 内容 |
| --- | --- |
| `src/`、`include/` | 板级驱动、PSRAM 初始化及 CanoKey 适配 |
| `src/boot/` | SPI 启动和 FEL 启动入口 |
| `ld/`、`cmake/` | 链接脚本和交叉编译配置 |
| `tools/` | 主机端打包、加载、备份和烧录脚本 |
| `tools/target/` | 在板上执行的 NOR 读写辅助程序 |
| `tests/` | 离线测试、板上测试及主机功能测试 |

## 测试

功能测试、依赖和运行方法见 [测试文档](tests/README.md)。
