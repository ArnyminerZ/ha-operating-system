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

# Fuel gauge of the PMI8998, the charger driver is blocked so only the gauge is there
BATTERY_PATH = "/sys/class/power_supply/qcom-battery"
BATTERY_INTERVAL = timedelta(seconds=30)
