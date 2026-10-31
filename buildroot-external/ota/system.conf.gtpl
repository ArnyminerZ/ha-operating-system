[system]
compatible={{ env "ota_compatible" }}
mountprefix=/run/rauc
statusfile=/mnt/boot/rauc.db
{{- if or (eq (env "BOOTLOADER") "tryboot") (eq (env "BOOTLOADER") "android") }}
bootloader=custom
{{- else }}
bootloader={{ env "BOOTLOADER" }}
{{- end }}
{{- if eq (env "BOOTLOADER") "grub" }}
grubenv=/mnt/boot/EFI/BOOT/grubenv
{{- end }}

{{- if eq (env "BOOTLOADER") "tryboot" }}
[handlers]
bootloader-custom-backend=/usr/lib/rauc/rpi-tryboot.sh

{{- end }}
{{- if eq (env "BOOTLOADER") "android" }}
[handlers]
bootloader-custom-backend=/usr/lib/rauc/android-slot.sh

{{- end }}
[keyring]
path=/etc/rauc/keyring.pem

[slot.boot.0]
device=/dev/disk/by-partlabel/hassos-boot
type=vfat
allow-mounted=true
{{- if eq (env "BOOT_SPL") "true" }}
[slot.spl.0]
device=/dev/disk/by-partlabel/hassos-boot
type=raw
{{- end }}

[slot.kernel.0]
device=/dev/disk/by-partlabel/hassos-kernel0
type=raw
bootname=A

[slot.rootfs.0]
device=/dev/disk/by-partlabel/hassos-system0
type=raw
parent=kernel.0

[slot.kernel.1]
device=/dev/disk/by-partlabel/hassos-kernel1
type=raw
bootname=B

[slot.rootfs.1]
device=/dev/disk/by-partlabel/hassos-system1
type=raw
parent=kernel.1
