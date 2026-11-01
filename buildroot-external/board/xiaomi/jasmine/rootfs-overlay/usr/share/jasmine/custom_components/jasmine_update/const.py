"""Constants for the Mi A2 HAOS updates integration."""

from datetime import timedelta

DOMAIN = "jasmine_update"

REPO = "ArnyminerZ/ha-operating-system"
TAG_PREFIX = "jasmine-"
RELEASES_URL = f"https://api.github.com/repos/{REPO}/releases?per_page=30"

# The share folder is visible to Core as /share and to the host and RAUC under
# /mnt/data/supervisor/share
SHARE_DIR = "/share/jasmine-update"
HOST_SHARE_DIR = "/mnt/data/supervisor/share/jasmine-update"
INSTALLED_FILE = "installed.json"
BUNDLE_FILE = "update.raucb"

SCAN_INTERVAL = timedelta(hours=6)
