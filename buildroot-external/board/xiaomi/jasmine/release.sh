#!/bin/bash
# Publish the images of the last build as a GitHub release of the fork.
#
#   release.sh [tag]
#
# Run it on the build host after "make xiaomi_jasmine". The tag defaults to
# jasmine-<date>-<n>. jasmine-update on the phone installs the newest release
# whose tag starts with "jasmine-". The bundle was signed by the build.
set -euo pipefail

REPO="${REPO:-ArnyminerZ/ha-operating-system}"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
IMAGES="${ROOT}/output/images"

TAG="${1:-}"
if [ -z "${TAG}" ]; then
    day=$(date +%Y%m%d)
    n=1
    while gh release view "jasmine-${day}-${n}" --repo "${REPO}" >/dev/null 2>&1; do
        n=$((n + 1))
    done
    TAG="jasmine-${day}-${n}"
fi

bundle=$(ls "${IMAGES}"/haos_xiaomi-jasmine-*.raucb)
VERSION=$(basename "${bundle}" .raucb)
VERSION=${VERSION#haos_xiaomi-jasmine-}
boot=$(ls "${IMAGES}"/haos_xiaomi-jasmine-*-boot-A.img)
disk=$(ls "${IMAGES}"/haos_xiaomi-jasmine-*.img.xz)

(cd "${IMAGES}" && sha256sum "$(basename "${bundle}")" > "${bundle}.sha256")

commit=$(git -C "${ROOT}" rev-parse --short HEAD)
notes=$(cat <<EON
Home Assistant OS ${VERSION} for the Xiaomi Mi A2, built from ${commit}.

Install on the phone: \`jasmine-update install --reboot\`

The update replaces the system partition only. When the boot image changed
(kernel or initramfs), flash it too with fastboot, its checksum is:

\`$(cd "${IMAGES}" && sha256sum "$(basename "${boot}")")\`

Files: the .raucb bundle is the update, the boot image and the disk image
(.img.xz) are for a fresh install, see board/xiaomi/jasmine/README.md.
EON
)

gh release create "${TAG}" --repo "${REPO}" --target "$(git -C "${ROOT}" rev-parse HEAD)" \
    --title "${TAG}" --notes "${notes}" \
    "${bundle}" "${bundle}.sha256" "${boot}" "${disk}"
