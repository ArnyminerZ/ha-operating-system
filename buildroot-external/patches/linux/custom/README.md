# Patches for boards building a custom kernel tarball

Buildroot uses `patches/linux/<version>/` instead of `patches/linux/` when the
directory exists, and the kernel version of a board that builds from a custom
tarball is `custom`.

These boards (Xiaomi Mi A2) build a kernel which already contains some of the
generic patches, e.g. the Bluetooth `0003` one, so only the patches that still
apply are linked here.
