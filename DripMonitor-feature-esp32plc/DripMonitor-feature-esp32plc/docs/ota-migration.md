# OTA Migration: Enabling MCUboot for the ESP32 PLC 21

This is the work that still needs to happen to take the DripMonitor build
from "single-image, esptool-flashed" to "dual-bank, MCUboot-managed,
remote-OTA-capable".  Captured here so we don't lose track of it during
the rest of the PLC bring-up.

## Status today

| Piece | State |
|---|---|
| Application has space for two images | ✅ partition layout from `partitions_0x1000_default_16M.dtsi` gives slot0 + slot1, each 7936 KB |
| `west sign` metadata in `west.yml` | ✅ `slot-size = 0x7C0000` matches the partition size |
| MCUboot enabled in app build (`prj.conf`) | ❌ `# CONFIG_BOOTLOADER_MCUBOOT=y` still commented out |
| MCUboot built alongside app via sysbuild | ❌ `Kconfig.sysbuild` defaults to `BOOTLOADER_ESP_IDF` (the ESP-IDF second-stage bootloader; no image swap support) |
| Signing key generated | 🟡 `west.yml`'s `sign:` block points at `/home/kladmin/dev/kltech.pem`. Same key works on ESP32 if it is RSA-2048/3072 or EC-256. Verify before reuse. |

## What needs to change

### 1. `prj.conf` — enable MCUboot integration in the application

Uncomment / add:

```text
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_FLASH_MAP=y
CONFIG_IMG_MANAGER=y
CONFIG_MCUMGR=y                # if you want to push updates over MCUmgr
CONFIG_MCUMGR_TRANSPORT_UDP=y  # for OTA over the FX30/Ethernet path
```

`CONFIG_MCUMGR_TRANSPORT_UDP=y` is the right transport given the FX30
gives us plain Ethernet; it lets `mcumgr image upload` push the new
image straight to the PLC over the cellular gateway.

### 2. `boards/industrialshields/esp32_plc_21/Kconfig.sysbuild` — switch the second-stage bootloader

```text
choice BOOTLOADER
    default BOOTLOADER_MCUBOOT
endchoice

choice BOOT_SIGNATURE_TYPE
    default BOOT_SIGNATURE_TYPE_RSA   # or _ECDSA_P256, match what kltech.pem actually is
endchoice
```

To check the existing key's algorithm:

```bash
openssl rsa -in /home/kladmin/dev/kltech.pem -text -noout      # if it's RSA
openssl ec  -in /home/kladmin/dev/kltech.pem -text -noout      # if it's EC
```

If the key is RSA-2048/3072 → use `BOOT_SIGNATURE_TYPE_RSA`.
If the key is EC-P256       → use `BOOT_SIGNATURE_TYPE_ECDSA_P256`.

### 3. Build with sysbuild, signing key passed into the MCUboot image

Standard `west build` only builds the application; sysbuild is what
builds MCUboot alongside it.  The MCUboot image needs the same key
that signs the application so it can verify slot0/slot1 signatures.

```bash
cd ~/zephyr-4.2.0
west build --sysbuild -b esp32_plc_21/esp32/procpu \
    -d ~/dev/DripMonitor/build/esp32_plc_21 \
    ~/dev/DripMonitor \
    -- -Dmcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/home/kladmin/dev/kltech.pem\"
```

Then sign the application image:

```bash
west sign -t imgtool -d ~/dev/DripMonitor/build/esp32_plc_21
```

This produces `zephyr.signed.bin` in the build dir.  That signed binary
is what goes into MCUmgr / OTA pushes; the unsigned `zephyr.bin` won't
be accepted by MCUboot once verification is on.

### 4. First flash after enabling MCUboot

`west flash` after sysbuild is enabled will flash MCUboot + the
application together via esptool.  After that first flash, future
images can be delivered over the wire (Ethernet → FX30 → cellular).

## Useful references

- Zephyr MCUboot docs: <https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html>
- Zephyr sysbuild + MCUboot on ESP32: <https://docs.zephyrproject.org/latest/build/sysbuild/index.html>
- Image signing with imgtool: <https://docs.mcuboot.com/imgtool.html>
- Partition layout we're using: `boards/industrialshields/esp32_plc_21/esp32_plc_21_esp32_procpu.dts` →
  `#include <espressif/partitions_0x1000_default_16M.dtsi>`
