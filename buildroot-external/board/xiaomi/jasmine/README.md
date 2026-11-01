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

## Known issues

- Bluetooth does not work. The controller needs firmware that is not packaged,
  and its driver (`hci_uart`) crashes the kernel Bluetooth code, which hangs
  Home Assistant. The driver is blocked with `module_blacklist` on the kernel
  command line, a file in `/etc/modprobe.d` does not stop it from loading.
- The charger driver (`qcom_smbx`) can crash with a NULL pointer while the
  charger properties are read, which kills the Supervisor at boot. It is
  blocked the same way. The charger hardware keeps charging on its own, only
  the battery level from the fuel gauge is available in Linux.
- The touchscreen does not work, its driver never registers an input device.
- The boot image (kernel) and the system partition (kernel modules) are
  installed separately, they must always be updated together.

## Screen timeout

`screen-timeout.service` switches the backlight off after 30 seconds without a
button press, and a volume key switches it on again. The touchscreen can not be
used for this, and the power key powers the device off. Change `TIMEOUT` in the
service to use another time.
