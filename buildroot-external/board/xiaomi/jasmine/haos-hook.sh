#!/bin/bash
# shellcheck disable=SC2155

# Android boot image parameters of the SDM660 bootloader (header version 0)
BOOT_PAGE_SIZE=0x1000
BOOT_KERNEL_ADDR=0x8000
BOOT_RAMDISK_ADDR=0x1000000
BOOT_SECOND_ADDR=0xf00000
BOOT_TAGS_ADDR=0x100

DTB_NAME="sdm660-xiaomi-jasmine.dtb"

function create_initramfs() {
    local initramfs_dir="${BINARIES_DIR}/initramfs"
    local cc=$(ls "${HOST_DIR}"/bin/*-linux-gnu-gcc | head -n 1)

    rm -rf "${initramfs_dir}"
    mkdir -p "${initramfs_dir}"/{dev,proc,sys,sysroot}

    "${cc}" -static -Os -Wall -o "${initramfs_dir}/init" "${BOARD_DIR}/initramfs/init.c"

    (cd "${initramfs_dir}" && find . | cpio -o -H newc --quiet --owner=0:0 | gzip -9) > "${BINARIES_DIR}/initramfs.cpio.gz"
}

# Create an Android boot image that boots the given RAUC slot
# $1: slot name (A or B)
function create_boot_image() {
    local slot="${1}"
    local cfg="${BINARIES_DIR}/bootimg-${slot}.cfg"
    local extra_cmdline="$(cat "${BOARD_DIR}/cmdline.txt")"
    local root_partuuid

    if [ "${slot}" == "A" ]; then
        root_partuuid="8d3d53e3-6d49-4c38-8349-aff6859e82fd"
    else
        root_partuuid="a3ec664e-32ce-4665-95ea-7ae90ce9aa20"
    fi

    cat > "${cfg}" <<EOT
pagesize = ${BOOT_PAGE_SIZE}
kerneladdr = ${BOOT_KERNEL_ADDR}
ramdiskaddr = ${BOOT_RAMDISK_ADDR}
secondaddr = ${BOOT_SECOND_ADDR}
tagsaddr = ${BOOT_TAGS_ADDR}
name =
cmdline = root=PARTUUID=${root_partuuid} rootfstype=erofs ro rootwait loop.max_part=16 zram.enabled=1 zram.num_devices=3 fsck.repair=yes ${extra_cmdline} rauc.slot=${slot}
EOT

    rm -f "$(haos_image_basename)-boot-${slot}.img"
    "${HOST_DIR}/bin/abootimg" --create "$(haos_image_basename)-boot-${slot}.img" \
        -f "${cfg}" \
        -k "${BINARIES_DIR}/Image.gz-dtb" \
        -r "${BINARIES_DIR}/initramfs.cpio.gz"
}

function haos_pre_image() {
    local BOOT_DATA="$(path_boot_dir)"

    cp "${BOARD_DIR}/cmdline.txt" "${BOOT_DATA}/cmdline.txt"

    # The bootloader expects a gzipped kernel with the device tree appended
    gzip -9 -c "${BINARIES_DIR}/Image" > "${BINARIES_DIR}/Image.gz"
    cat "${BINARIES_DIR}/Image.gz" "${BINARIES_DIR}/${DTB_NAME}" > "${BINARIES_DIR}/Image.gz-dtb"

    create_initramfs
}

function haos_post_image() {
    create_boot_image A
    create_boot_image B

    # The disk image is flashed to the userdata partition, see the README
    convert_disk_image_xz
}
