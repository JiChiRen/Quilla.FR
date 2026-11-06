# 出厂镜像打包（`tools/qboot_image.py`）

`qboot_image.py` 只依赖 Python 标准库。它生成首片 META 记录（QMTL + QMF1），把 QBOOT、META、APP 合成一个从 `0x08000000` 起始的 128 KiB 镜像，用于整片擦除后一次烧录。

## 命令

```
python tools/qboot_image.py selftest

python tools/qboot_image.py pack \
    --boot <QBOOT_CHECK.bin> \
    --app  appliance/gcc/build/quilla-fr-app.bin \
    --out  combined.bin [--meta-out meta_record.bin] [--version 0]

python tools/qboot_image.py verify --image combined.bin
```

`--meta-base` / `--app-start` 可覆盖默认的 `0x08006000` / `0x08007000`；QBOOT 固定 `0x08000000`。改动这两项时，QBOOT 构建宏与 APP 链接脚本必须同步修改。

烧录前后各跑一次 `verify`，可区分是镜像问题还是烧录问题。校验项包括整片长度、两个向量表、记录 CRC、`image_len` 越界检查，以及 APP 区 SHA-256 与 manifest 的一致性。

## 布局

```
0x08000000  QBOOT  24 KiB（原样拷贝，尾部 0xFF 填充）
0x08006000  META    4 KiB（首条 256 B 记录，其余 0xFF）
0x08007000  APP   100 KiB（image_len = APP bin 实际长度）
0x08020000  128 KiB Flash 结束
```

256 B 记录由 64 B QMTL 块（magic、seq、flag、floor、seal、CRC32）与 128 B QMF1 块（magic、version、`image_len`、SHA-256、签名、CRC32）组成。CRC32 采用 Quilla 使用的 IEEE 0xEDB88320 变体。

## 变体

工具按 QBOOT bin 内的 banner 串自动判定变体。

| 变体 | 签名 | APP secver | 串口升级完成码 |
| --- | --- | --- | --- |
| CHECK | 64 字节全 0 | 不要求 | `ACK_END 0x83` |
| SEALED | Ed25519，预置 seal | 必需 | `ACK_SIGN 0x84` |
| SEC | Ed25519，不预置 seal | 必需 | `ACK_SIGN 0x84` |

SEALED / SEC 需传 `--key <private.hex>`。工具会检查偏移 `0x400` 的 secver 区，对 36 B 描述子 `image_len(4, LE) || SHA-256(image)` 签名，用派生公钥自验，并把 manifest 版本与 floor 取为镜像内的 secver 版本。`--no-seal` 可关闭 SEALED 的预置 seal。

私钥必须与编译进 QBOOT 的公钥配对。仓库默认密钥对（`quilla/tools/_keytest/`）公开，仅供开发验证。

## 安全边界说明

CHECK 只校验传输完整性，不校验镜像来源，也不防回滚，不构成安全启动。SEALED / SEC 增加签名校验与单调递增的版本下限。
