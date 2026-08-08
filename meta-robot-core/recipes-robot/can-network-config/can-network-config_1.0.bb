SUMMARY = "systemd-networkd configuration that brings up the CAN interfaces at boot"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://80-can.network \
    file://80-robot-can.preset \
"

S = "${WORKDIR}"

inherit allarch features_check
REQUIRED_DISTRO_FEATURES = "systemd"

do_install() {
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/80-can.network ${D}${sysconfdir}/systemd/network/

    # Make sure systemd-networkd is enabled in the image (applied by systemctl preset-all).
    install -d ${D}${systemd_unitdir}/system-preset
    install -m 0644 ${WORKDIR}/80-robot-can.preset ${D}${systemd_unitdir}/system-preset/
}

FILES:${PN} += "${systemd_unitdir}/system-preset"
CONFFILES:${PN} = "${sysconfdir}/systemd/network/80-can.network"

RRECOMMENDS:${PN} = "kernel-module-can-dev kernel-module-mcp251xfd"
