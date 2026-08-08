SUMMARY = "Robot core controller - SocketCAN control daemon"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# The application source lives at <repo>/app, outside the layer, so it can
# also be built natively. Point the fetcher at the repo root.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:${THISDIR}/files:"

SRC_URI = " \
    file://app/ \
    file://robot-core-controller.service \
    file://robot-core-controller.default \
"

S = "${WORKDIR}/app"

inherit cmake systemd

SYSTEMD_SERVICE:${PN} = "robot-core-controller.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-core-controller.service ${D}${systemd_system_unitdir}/

    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/robot-core-controller.default \
        ${D}${sysconfdir}/default/robot-core-controller
}

CONFFILES:${PN} = "${sysconfdir}/default/robot-core-controller"

RDEPENDS:${PN} = "can-network-config"
RRECOMMENDS:${PN} = "kernel-module-can-raw"
