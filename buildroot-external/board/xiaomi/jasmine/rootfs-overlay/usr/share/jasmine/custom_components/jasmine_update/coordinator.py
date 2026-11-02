"""Look for new releases of the fork on GitHub."""

from __future__ import annotations

from dataclasses import dataclass
import json
import logging
from pathlib import Path

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from . import rauc
from .const import (
    BATTERY_INTERVAL,
    BATTERY_PATH,
    DOMAIN,
    INSTALLED_FILE,
    RELEASES_URL,
    SCAN_INTERVAL,
    SHARE_DIR,
    TAG_PREFIX,
)

_LOGGER = logging.getLogger(__name__)


@dataclass
class Release:
    """The newest release of the fork."""

    tag: str
    url: str
    notes: str
    bundle_url: str | None
    checksum_url: str | None


@dataclass
class UpdateData:
    """What the coordinator knows."""

    release: Release
    boot_slot: str
    installed: str | None


@dataclass
class RuntimeData:
    """Coordinators of the config entry."""

    updates: JasmineCoordinator
    battery: BatteryCoordinator


type JasmineConfigEntry = ConfigEntry[RuntimeData]


def read_installed(slot: str) -> str | None:
    """Release tag recorded for a slot, written by this integration and jasmine-update."""
    try:
        data = json.loads(Path(SHARE_DIR, INSTALLED_FILE).read_text())
    except (OSError, ValueError):
        return None
    return data.get(slot)


def record_installed(slot: str, tag: str) -> None:
    """Remember which release a slot got."""
    path = Path(SHARE_DIR, INSTALLED_FILE)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        data = {}
    data[slot] = tag
    path.write_text(json.dumps(data))


class JasmineCoordinator(DataUpdateCoordinator[UpdateData]):
    """Fetch the latest release and the installed one."""

    config_entry: JasmineConfigEntry

    def __init__(self, hass: HomeAssistant, entry: JasmineConfigEntry) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN, update_interval=SCAN_INTERVAL)

    async def _async_update_data(self) -> UpdateData:
        session = async_get_clientsession(self.hass)
        try:
            async with session.get(RELEASES_URL, headers={"Accept": "application/vnd.github+json"}) as response:
                response.raise_for_status()
                releases = await response.json()
            boot_slot = await rauc.get_boot_slot()
        except Exception as err:
            raise UpdateFailed(f"Cannot check for updates: {err}") from err

        for item in releases:
            if item["draft"] or item["prerelease"] or not item["tag_name"].startswith(TAG_PREFIX):
                continue
            assets = {asset["name"]: asset["browser_download_url"] for asset in item["assets"]}
            bundle = next((url for name, url in assets.items() if name.endswith(".raucb")), None)
            checksum = next((url for name, url in assets.items() if name.endswith(".raucb.sha256")), None)
            release = Release(item["tag_name"], item["html_url"], item.get("body") or "", bundle, checksum)
            break
        else:
            raise UpdateFailed(f"No {TAG_PREFIX}* release found")

        installed = await self.hass.async_add_executor_job(read_installed, boot_slot)
        return UpdateData(release, boot_slot, installed)


def read_battery() -> dict[str, float]:
    """Read the fuel gauge, values the driver does not report are left out."""
    scale = {
        "capacity": 1,
        "voltage_now": 1e-6,
        "current_now": 1e-6,
        "temp": 0.1,
        "charge_full_design": 1e-6,
    }
    data: dict[str, float] = {}
    for name, factor in scale.items():
        try:
            data[name] = int(Path(BATTERY_PATH, name).read_text()) * factor
        except (OSError, ValueError):
            continue
    if "voltage_now" in data and "current_now" in data:
        data["power"] = data["voltage_now"] * data["current_now"]
    return data


class BatteryCoordinator(DataUpdateCoordinator[dict[str, float]]):
    """Poll the fuel gauge."""

    config_entry: JasmineConfigEntry

    def __init__(self, hass: HomeAssistant, entry: JasmineConfigEntry) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=f"{DOMAIN}_battery", update_interval=BATTERY_INTERVAL)

    async def _async_update_data(self) -> dict[str, float]:
        data = await self.hass.async_add_executor_job(read_battery)
        if not data:
            raise UpdateFailed("No battery data")
        return data
