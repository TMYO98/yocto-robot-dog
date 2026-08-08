SUMMARY = "Robot core controller image for Raspberry Pi with a CAN FD HAT"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-dropbear"

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    ${CORE_IMAGE_EXTRA_INSTALL} \
    robot-core-controller \
    can-network-config \
    can-utils \
    iproute2 \
    kernel-module-can-raw \
    kernel-module-can-dev \
    kernel-module-mcp251xfd \
    kernel-module-mcp251x \
"
