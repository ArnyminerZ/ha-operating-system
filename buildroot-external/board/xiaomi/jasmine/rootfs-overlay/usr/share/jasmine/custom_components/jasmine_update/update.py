"""Update entity that installs releases of the fork with RAUC."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
from typing import Any

from homeassistant.components.update import UpdateEntity, UpdateEntityFeature
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import rauc
from .const import BUNDLE_FILE, DOMAIN, HOST_SHARE_DIR, SHARE_DIR
from .coordinator import JasmineConfigEntry, JasmineCoordinator, record_installed


async def async_setup_entry(
    hass: HomeAssistant,
    entry: JasmineConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Set up the update entity."""
    async_add_entities([JasmineUpdate(entry.runtime_data)])


class JasmineUpdate(CoordinatorEntity[JasmineCoordinator], UpdateEntity):
    """Home Assistant OS releases for the Mi A2."""

    _attr_has_entity_name = True
    _attr_name = "Home Assistant OS"
    _attr_title = "Home Assistant OS (Mi A2)"
    _attr_supported_features = (
        UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS | UpdateEntityFeature.RELEASE_NOTES
    )
    _attr_unique_id = "jasmine_haos_update"
    _attr_device_info = DeviceInfo(identifiers={(DOMAIN, "jasmine")}, name="Xiaomi Mi A2", manufacturer="Xiaomi")

    def __init__(self, coordinator: JasmineCoordinator) -> None:
        """Initialize the entity."""
        super().__init__(coordinator)
        self._attr_in_progress = False
        self._attr_update_percentage: int | None = None

    @property
    def installed_version(self) -> str | None:
        """Release running now, unknown when it was flashed by hand."""
        return self.coordinator.data.installed or "unknown"

    @property
    def latest_version(self) -> str:
        """Newest release."""
        return self.coordinator.data.release.tag

    @property
    def release_url(self) -> str:
        """Page of the newest release."""
        return self.coordinator.data.release.url

    async def async_release_notes(self) -> str | None:
        """Notes of the newest release."""
        return self.coordinator.data.release.notes

    def _progress(self, percent: int) -> None:
        self._attr_update_percentage = percent
        self.async_write_ha_state()

    async def async_install(self, version: str | None, backup: bool, **kwargs: Any) -> None:
        """Download the bundle, install it into the other slot and reboot."""
        data = self.coordinator.data
        release = data.release
        if not release.bundle_url:
            raise HomeAssistantError(f"Release {release.tag} has no bundle")
        target = "A" if data.boot_slot == "B" else "B"

        self._attr_in_progress = True
        self._attr_update_percentage = None
        self.async_write_ha_state()
        bundle = Path(SHARE_DIR, BUNDLE_FILE)
        try:
            await self._download(release.bundle_url, release.checksum_url, bundle)
            await rauc.install_bundle(f"{HOST_SHARE_DIR}/{BUNDLE_FILE}", self._progress)
            await self.hass.async_add_executor_job(record_installed, target, release.tag)
        finally:
            await self.hass.async_add_executor_job(lambda: bundle.unlink(missing_ok=True))
            self._attr_in_progress = False
            self._attr_update_percentage = None
            self.async_write_ha_state()

        # The new slot boots after a reboot and the old one stays as fallback
        await self.hass.services.async_call("hassio", "host_reboot", blocking=False)

    async def _download(self, url: str, checksum_url: str | None, path: Path) -> None:
        session = async_get_clientsession(self.hass)
        await self.hass.async_add_executor_job(lambda: os.makedirs(path.parent, exist_ok=True))
        digest = hashlib.sha256()
        async with session.get(url) as response:
            response.raise_for_status()
            handle = await self.hass.async_add_executor_job(path.open, "wb")
            try:
                async for chunk in response.content.iter_chunked(1 << 20):
                    digest.update(chunk)
                    await self.hass.async_add_executor_job(handle.write, chunk)
            finally:
                await self.hass.async_add_executor_job(handle.close)
        if checksum_url:
            async with session.get(checksum_url) as response:
                response.raise_for_status()
                expected = (await response.text()).split()[0]
            if expected != digest.hexdigest():
                raise HomeAssistantError("Checksum of the downloaded bundle does not match")
