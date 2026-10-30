# robot_core_controler

Yocto (scarthgap) image for a Raspberry Pi with a CAN FD HAT. It runs `robot-core-controller`, a SocketCAN daemon.

```
robot-core.yml                     kas build config (machine, layers, CAN HAT setup)
app/                               C source (CMake), can also be built natively
meta-robot-core/
  conf/layer.conf
  recipes-core/images/robot-core-image.bb
  recipes-robot/robot-core-controller/   app recipe + systemd unit + /etc/default config
  recipes-robot/can-network-config/      systemd-networkd .network that brings up can0/can1
```

## CAN HAT selection

| HAT | Controller | Channels | CAN FD | Isolated | Notes |
|---|---|---|---|---|---|
| **Waveshare 2-CH CAN FD HAT** (recommended) | MCP2518FD | 2 | yes | yes, 5 kV | Mainline `mcp251xfd` driver. The image is configured for this one. |
| SK Pang / Copperhill PiCAN FD | MCP2518FD | 1 (Duo: 2) | yes | Isolated variants exist | Good docs, more expensive |
| Waveshare RS485 CAN HAT | MCP2515 | 1 | no | no | Cheap, classic CAN up to 1 Mbit/s only |
| USB adapter (e.g. CANable, gs_usb) | — | 1 | some | some | Not a HAT, but needs no device tree setup |

**Why the Waveshare 2-CH CAN FD HAT:** it supports CAN FD, so you won't outgrow it. It has two channels, so you can split motor and sensor buses. The bus side is galvanically isolated, which matters near motor drivers. It also uses the in-kernel `mcp251xfd` driver, with no out-of-tree code.

Hardware setup for this HAT:
- Set the **VIO jumper to 3.3V**.
- Keep the factory **mode A** (two SPI buses). The config assumes `can0` = SPI0/GPIO25 and `can1` = SPI1/GPIO24.
- Enable the **120 Ω terminator** jumper only if the Pi is at one end of the bus.
- Wire CANH, CANL **and G** (isolated ground) to the other nodes.

To use the MCP2515 HAT instead, swap the `can-hat` block in `robot-core.yml` for the commented alternative and set `FDMode=no`.

## Build

Yocto needs Linux. On Windows, use **WSL2 (Ubuntu 22.04/24.04)**. Clone the repo into the WSL filesystem (`~/`), not `/mnt/d/...`, because NTFS is very slow and is case-insensitive. You need about 100 GB free disk.

```bash
sudo apt install gawk wget git diffstat unzip texinfo gcc build-essential chrpath \
  socat cpio python3 python3-pip python3-pexpect xz-utils debianutils iputils-ping \
  python3-git python3-jinja2 python3-subunit zstd liblz4-tool file locales libacl1 bmap-tools
sudo locale-gen en_US.UTF-8
pip install --user kas

git init        # kas expects the config to live in a repo
kas build robot-core.yml
```

For a Pi 5, change `machine:` to `raspberrypi5`.

The output is `build/tmp/deploy/images/raspberrypi4-64/robot-core-image-raspberrypi4-64.rootfs.wic.bz2`.

Flash it with:

```bash
sudo bmaptool copy robot-core-image-*.wic.bz2 /dev/sdX
```

You can also use Raspberry Pi Imager → "Use custom" with the `.wic.bz2` file.

## Verify on the target

Log in over SSH or the serial console as `root`, with no password (development image only).

```bash
dmesg | grep -i mcp251xfd         # both controllers probed
ip -details link show can0        # state ERROR-ACTIVE, bitrate 1000000, dbitrate 2000000
systemctl status robot-core-controller
journalctl -u robot-core-controller -f
candump -td any                   # heartbeats on 0x701
cansend can0 601##1DEADBEEF       # FD "ping" -> controller echoes on 0x581
```

To loop back without a second node, connect can0 to can1 (H–H, L–L, G–G) with termination enabled on both.

## Configuration

- **Bitrates:** `meta-robot-core/recipes-robot/can-network-config/files/80-can.network`. The defaults are 1 Mbit/s nominal and 2 Mbit/s data, with auto bus-off recovery.
- **Interfaces, node id and loop period:** `/etc/default/robot-core-controller`.
- **Robot logic:** `handle_frame()` and `on_tick()` in `app/src/main.c`.

Iterate on the app without reflashing:

```bash
kas shell robot-core.yml -c "bitbake robot-core-controller"
```

Then copy the binary from `build/tmp/work/*/robot-core-controller/1.0/image/usr/bin/` to the Pi with `scp`.

The kernel names `can0` and `can1` in probe order. That order is normally stable. If you need guaranteed names, add a systemd `.link` file that matches on `Path=`.
