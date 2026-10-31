# Xiaomi Mi A2 (jasmine)

HAOS for the Xiaomi Mi A2 (Snapdragon 660), booted through the Android
bootloader.

- The kernel comes from [sdm660-mainline](https://github.com/sdm660-mainline/linux)
  (the tree postmarketOS uses), `kernel-base.config` is the postmarketOS config.
- `patches/` is applied on top of it, it forces the USB controller into host
  mode so no `usb-force-host` workaround is needed.
- The Android bootloader loads `boot.img` (gzipped kernel with appended device
  tree and a small initramfs). The initramfs attaches the HAOS disk image
  stored in the `userdata` partition as a loop device and mounts the system slot
  given by `rauc.slot=`.

## Install

Decompress the disk image (`xz -d`), then flash it to `userdata` and the boot
image to the Android slot that is active (the other slot can stay unused):

```
fastboot erase userdata
fastboot flash userdata haos_xiaomi-jasmine-<version>.img
fastboot flash boot_b haos_xiaomi-jasmine-<version>-boot-A.img
```

The previous contents of `userdata` are overwritten. The boot image name refers
to the HAOS slot (`rauc.slot=A`), not to the Android slot.

To try HAOS without touching `userdata`, put the disk image into another
partition and boot it from RAM with the disk name on the kernel command line,
for example `haos.disk=system_a` and `fastboot boot`.

`qbootctl.service` marks the Android boot slot as successful on every boot,
otherwise the bootloader runs out of boot attempts and falls back to fastboot.
`panic=10` makes a crashing kernel reboot, which also uses up a boot attempt.
