# F101 功能测试

以下命令均从仓库根目录执行。

## USB 与应用

Python 依赖见 `ports/k230d/tests/requirements.txt`；PIV 测试还需要
`yubico-piv-tool`。运行时只连接待测试的 CanoKey，以下测试假定使用默认 PIN。

```sh
python3 ports/k230d/tests/host/host_check.py --oath-test
python3 ports/k230d/tests/host/identity_check.py
python3 ports/k230d/tests/host/console_check.py --transport webusb \
  --product 'CanoKey F101 YuzukiNeko' --settings
python3 ports/f101/tests/host/fido_sign_check.py --debug-presence \
  --serial /dev/ttyUSB0
python3 ports/k230d/tests/host/fido_credential_check.py --port f101 \
  --destructive --debug-presence
python3 ports/k230d/tests/host/piv_rsa_check.py --destructive --bits 2048 3072 4096
python3 ports/f101/tests/host/openpgp_sign_check.py --generate-if-empty
python3 ports/f101/tests/host/ndef_check.py
```

这些测试会写入所选存储后端，包括持久化 NOR：

- FIDO 签名测试会安装新的自签名开发证明，只在本地请求需要时注入串口确认。
- 扩展 FIDO 测试会在前后重置 FIDO 应用，清空其凭据和 PIN。
- PIV 测试覆盖 `9e` 槽。
- OpenPGP 测试只在空签名槽生成 Ed25519 测试密钥，并保留它。
  不带 `--generate-if-empty` 时，仅使用已有 Ed25519 密钥验签。
- NDEF 测试会先保存完整原文件，测试结束后恢复并校验。

跨重启的 HOTP 测试：

```sh
python3 ports/f101/tests/host/nor_persistence_check.py prepare
# 正常复位；若使用 FEL 加载方式，则重新加载同一固件。
python3 ports/f101/tests/host/nor_persistence_check.py verify
```

`verify` 检查公开测试凭据和计数器保留，随后删除该测试项。
已配置开发证明时，还可以验证 FIDO 凭据跨 SPI 启动后的签名：

```sh
python3 ports/f101/tests/host/boot_check.py prepare --debug-presence \
  --state ports/f101/build/spi-credential.json
# 正常复位，不进入 FEL，不从主机加载固件。
python3 ports/f101/tests/host/boot_check.py verify --debug-presence \
  --state ports/f101/build/spi-credential.json
```

## 烧录工具

离线测试使用 Python 标准库，检查镜像校验、芯片选择、日志恢复及写入失败处理：

```sh
python3 -m unittest discover -s ports/f101/tests/unit -p 'test_*.py' -v
```

## 随机数状态日志

状态更新和中断写入的主机测试需要 OpenSSL 开发库：

```sh
cc -Wall -Wextra -Werror -Iports/f101/include \
  -Icanokey-core/canokey-crypto/include \
  ports/f101/tests/unit/seed_journal_test.c ports/f101/src/seed_journal.c \
  -lcrypto -o /tmp/f101-seed-test
/tmp/f101-seed-test
```

## AES 后端

后端独立测试包含标准向量、交替密钥与加解密方向、原地/非对齐缓冲区、
CBC/CTR 和确定性 CTR_DRBG 摘要，并检查硬件密钥清理：

```sh
cmake --build ports/f101/build --target f101-aes-test
python3 ports/f101/tools/load_firmware.py ports/f101/build/f101-aes-test.bin
```

用另一构建目录配置 `-DF101_CE_AES=OFF`，可运行同一测试作软件对照。
每次运行均返回 FEL，不写 NOR。
