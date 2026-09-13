# CanoKey on BPI K230D Zero

RV64 裸机移植，运行于 CPU0 的片上 SRAM，使用 USB0 提供 FIDO2、CCID 和
WebUSB 接口。支持 RAM 和 microSD 两种存储后端，复用仓库锁定的 CanoKey
core、crypto、littlefs 和 TinyUSB。

这是开发版本。实体按键、NFC 和 RGB LED 尚未实现，用户确认通过 JTAG
注入；SD 数据未加密。当前版本尚未完成独立 SD 启动、手机兼容性和掉电
一致性验证。长 APDU 写入仍有偶发 PC/SC 事务错误需要排查。

## 目录

- `src/`：固件运行时、USB 适配入口和存储驱动。
- `include/`：端口头文件及原始存储布局。
- `tests/unit/`：在主机运行的 C 单元测试。
- `tests/host/`：通过 USB/JTAG 测试设备的 Python 脚本。
- `tests/target/`：运行在板上的硬件测试程序。
- `tools/`：镜像打包、固件加载和调试辅助工具。
- `cmake/`、`ld/`、`openocd/`：工具链、链接脚本和调试器配置。
- `build/`：构建产物。

## 构建

以下命令从 `canokey-stm32` 目录执行。需要支持 Zba/Zbb/Zbc/Zbs 的
RISC-V bare-metal GCC、picolibc、CMake 和 Python 3。

```sh
git submodule update --init canokey-core
git -C canokey-core config submodule.tinyusb.url https://github.com/hathach/tinyusb.git
git -C canokey-core submodule update --init --recursive canokey-crypto littlefs tinycbor
git -C canokey-core submodule update --init tinyusb
cmake -S ports/k230d -B ports/k230d/build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build ports/k230d/build --target canokey-k230d-image canokey-k230d-sd-image -j
```

`CROSS_COMPILE` 可指定工具链前缀。TinyUSB 使用上游镜像获取相同的锁定
提交；USB 和序列号适配在构建目录生成。

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `BUILD_SDIO_BACKEND` | `ON` | 构建 SD 固件及原始扇区测试程序 |
| `BUILD_CANOKEY_CORE` | `ON` | 关闭后仅构建 SDIO 测试程序 |
| `K230D_CACHE_ENABLED` | `ON` | 启用 C908 缓存 |
| `K230D_BITMANIP` | `ON` | 启用 Zba/Zbb/Zbc/Zbs，保持 LP64 ABI |
| `K230D_CRYPTO_SPEED` | `ON` | 密码库使用 `-O2` |
| `K230D_SD_CLOCK_HZ` | `25000000` | MMC1 数据时钟，最大 25 MHz |

## 镜像与刷卡

产物位于 `ports/k230d/build`：

| 文件 | 用途 |
| --- | --- |
| `canokey-k230d.elf` | RAM 后端，重载后丢失数据 |
| `canokey-k230d-sd.elf` | SD 后端，可通过 JTAG 加载 |
| `canokey-k230d.img` | RAM 后端的 2 MiB 启动镜像 |
| `canokey-k230d-sd.img` | 2 MiB 固件更新镜像，保留持久数据 |
| `canokey-k230d-sd-factory.img` | 5 MiB 出厂镜像，清空持久数据 |

镜像附带 `.sha256` 校验文件和 `.layout.json` 布局。BootROM 镜像采用
非加密格式，主副本位于 1 MiB，备用副本位于 1.5 MiB，每个槽为 512 KiB。
littlefs 直接使用 4 MiB 起的 1 MiB 原始扇区，不需要分区表或 FAT。

将卡接到读卡器，选择**整卡设备**并替换下面的 `/dev/sdX`：

```sh
# 首次初始化或重置凭据：清空数据区。
sudo dd if=ports/k230d/build/canokey-k230d-sd-factory.img of=/dev/sdX bs=4M conv=fsync status=progress
# 更新固件：保留数据区。
sudo dd if=ports/k230d/build/canokey-k230d-sd.img of=/dev/sdX bs=4M conv=fsync status=progress
```

板卡启动选择为 SD Card（BOOT0=1、BOOT1=1），参见
[板卡资料与原理图](https://docs.banana-pi.org/en/BPI-CanMV-K230D/BananaPi_BPI-CanMV-K230D-Zero)。
出厂镜像包含一次性格式化标记。首次启动需格式化和创建应用数据，完成后
才启用 USB；损坏的已有文件系统不会被自动格式化。

## JTAG 调试

### CH347F

需要启用 `ch347` 驱动的 OpenOCD，并为 USB 设备 `1a86:55de` 配置 udev
访问权限。配置使用 1.875 MHz JTAG 时钟，连接 CPU0；GDB 和 TCL 服务仅监听本机。

```sh
/path/to/openocd/src/openocd -s /path/to/openocd/tcl \
  -f ports/k230d/openocd/ch347.cfg
```

### CKLink

需要包含 CKLink 驱动及复合设备 bulk IN 端点修复的 OpenOCD，并为 USB
设备 `32bf:b210` 配置 udev 访问权限。

```sh
/path/to/openocd/src/openocd -s /path/to/openocd/tcl \
  -f ports/k230d/openocd/cklink.cfg -c "riscv set_mem_access progbuf"
```

连接超时时可尝试降低 `adapter speed` 至 200。该 CKLink 驱动未实现
物理 TRST 控制。

### 加载固件

启动所选探针的 OpenOCD 后，在另一个终端执行：

```sh
python3 ports/k230d/tools/load_firmware.py ports/k230d/build/canokey-k230d-sd.elf
```

加载器会清理缓存，下载并校验完整 ELF；重载时不要只跳回 `_start`，
否则 `.data` 中的初始化状态不会恢复。

确认输入为 `firmware_state.touch_request=1`（短按）或 `2`（长按），
一秒后过期。没有自动确认模式，固件不访问 INT0/PMU 按键。

## 测试

主机需要 PC/SC 服务、`libpcsclite-dev` 和 Python 环境。

```sh
python3 -m venv ports/k230d/.venv
ports/k230d/.venv/bin/pip install -r ports/k230d/tests/requirements.txt
ports/k230d/.venv/bin/python ports/k230d/tests/host/host_check.py --oath-test
ports/k230d/.venv/bin/python ports/k230d/tests/host/console_check.py --transport webusb
ports/k230d/.venv/bin/python ports/k230d/tests/host/identity_check.py
```

Console 测试的 `--settings` 使用默认管理员 PIN `123456`。WebUSB 测试前
关闭其他占用该设备的 Console/浏览器连接。

SD 数据持久化测试使用公开 HOTP 向量，检查完成后删除测试条目：

```sh
ports/k230d/.venv/bin/python ports/k230d/tests/host/persistence_check.py prepare
python3 ports/k230d/tools/load_firmware.py ports/k230d/build/canokey-k230d-sd.elf
ports/k230d/.venv/bin/python ports/k230d/tests/host/persistence_check.py check
```

FIDO2 签名测试会使用默认管理员 PIN，**替换开发证明密钥和证书**，并
通过 JTAG 确认本地请求。只应在开发设备上运行：

```sh
ports/k230d/.venv/bin/python ports/k230d/tests/host/fido_sign_check.py --debug-presence
# 可选：连续断言计时，每次都在主机验签。
ports/k230d/.venv/bin/python ports/k230d/tests/host/fido_sign_check.py --debug-presence --benchmark 20
python3 ports/k230d/tools/boot_status.py
```

签名计时区分设备 `ecc_sign` 和完整主机往返；后者包含模拟确认、USB 和
存储开销。启动计时不含 BootROM、入口汇编和主机枚举时间。

原始 SDIO 测试使用持久区之后的 16 个测试扇区：

```sh
cmake --build ports/k230d/build --target k230d-sd-probe
python3 ports/k230d/tools/load_firmware.py ports/k230d/build/k230d-sd-probe.elf
python3 ports/k230d/tools/openocd_command.py 'sleep 1500' 'halt'
python3 ports/k230d/tests/host/sd_host.py --write-test --stress 128
# 测试结束后恢复完整固件。
python3 ports/k230d/tools/load_firmware.py ports/k230d/build/canokey-k230d-sd.elf
```

`sd_host.py --initialize-raw` 会清除 LBA0 并写入格式化标记，仅用于明确
需要清空测试卡的场合。只读缓存的主机单元测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -Icanokey-core/include -Icanokey-core/littlefs -Iports/k230d/include \
  ports/k230d/tests/unit/sd_cache_test.c -o /tmp/k230d-sd-cache-test
/tmp/k230d-sd-cache-test
```

FIDO2 凭据管理测试会重置 FIDO 应用及其 PIN、凭据和证明资料，使用默认
管理员 PIN。它覆盖 PIN 协议 1/2、驻留凭据、枚举、删除和分片 Large Blob：

```sh
ports/k230d/.venv/bin/python ports/k230d/tests/host/fido_credential_check.py \
  --destructive --debug-presence
```

PIV 长操作回归测试需要系统安装 `yubico-piv-tool`，会覆盖测试设备的
9e 槽，使用默认 PIV 管理密钥和槽策略：

```sh
ports/k230d/.venv/bin/python ports/k230d/tests/host/piv_rsa_check.py --destructive
```

测试卡内 RSA3072/4096 生成，并在主机核对签名和解密结果。密钥生成可能
持续数十秒，固件在密码运算间隙继续处理 USB 和 CCID 等待响应。

## 实现说明

CPU0 使用 800 MHz 时钟和 SDK 的 C908 缓存配置。USB0 为全速 PIO，MMC1
为 3.3 V、4-bit PIO。littlefs 后端缓存 64 个只读扇区；写入同步落卡，
写入和擦除前使对应缓存失效。所有应用初始化完成后才启动 USB。

芯片序列号读取 SDK 定义的公开 PUF UID 槽 0，不写 OTP 或注册 PUF。
按每个 32 位字的大端顺序编码 UID，再计算：

```text
identity = SHA-256(ASCII("CanoKey K230D serial v1") || 00 || UID[32])
serial = identity[0:4]
```

USB、Admin、OpenPGP 和 PIV 共用该 4 字节序列号；Admin `0032010000`
返回完整 32 字节摘要供 Console 显示。它不依赖 SD 卡或 `sn` 文件，管理员
写序列号命令返回 `6985`。短序列号可能碰撞，需更强区分能力时使用完整
摘要。域字符串和字节序是持久身份格式，不应随意更改。
