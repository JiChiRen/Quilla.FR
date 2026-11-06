# Factory image packing (`tools/qboot_image.py`)

`qboot_image.py` is a pure-standard-library tool. It writes the first META
record (QMTL + QMF1) and merges QBOOT, META and APP into one 128 KiB image
starting at `0x08000000`, ready for a full-chip erase and a single flash.

## Commands

```
python tools/qboot_image.py selftest

python tools/qboot_image.py pack \
    --boot <QBOOT_CHECK.bin> \
    --app  appliance/gcc/build/quilla-fr-app.bin \
    --out  combined.bin [--meta-out meta_record.bin] [--version 0]

python tools/qboot_image.py verify --image combined.bin
```

`--meta-base` and `--app-start` override the defaults `0x08006000` and
`0x08007000`; QBOOT is fixed at `0x08000000`. Changing them requires matching
changes in the QBOOT build defines and the APP linker script.

Run `verify` before and after flashing to separate image errors from flashing
errors. It re-checks image length, both vector tables, record CRCs,
`image_len` bounds, and SHA-256 of the APP region against the manifest.

## Layout

```
0x08000000  QBOOT  24 KiB  (copied verbatim, 0xFF padding)
0x08006000  META    4 KiB  (first 256 B record, rest 0xFF)
0x08007000  APP   100 KiB  (image_len = actual APP bin length)
0x08020000  end of 128 KiB Flash
```

The 256 B record contains a 64 B QMTL block (magic, sequence, flag, floor,
seal, CRC32) and a 128 B QMF1 block (magic, version, `image_len`, SHA-256,
signature, CRC32). CRC32 is the IEEE 0xEDB88320 variant used by Quilla.

## Variants

The tool detects the variant from the QBOOT banner string inside the binary.

| Variant | Signature | APP secver | Completion code on serial upgrade |
| --- | --- | --- | --- |
| CHECK | 64 zero bytes | not required | `ACK_END 0x83` |
| SEALED | Ed25519, seal pre-loaded | required | `ACK_SIGN 0x84` |
| SEC | Ed25519, no seal pre-load | required | `ACK_SIGN 0x84` |

For SEALED / SEC pass `--key <private.hex>`. The tool verifies the secver
region at offset `0x400`, signs the 36 B descriptor
`image_len(4, LE) || SHA-256(image)`, self-verifies with the derived public
key, and takes manifest version and floor from the image's secver version.
`--no-seal` disables the SEALED pre-loaded seal.

The private key must match the public key compiled into QBOOT. The repository
default keypair (`quilla/tools/_keytest/`) is public and is for development
only.

## Safety statement

CHECK verifies transport integrity only; it does not authenticate the image
and does not enforce anti-rollback. It is not secure boot. SEALED / SEC add
signature verification and a monotonic version floor.
