"""Update entity for Home Assistant OS on the Xiaomi Mi A2."""

from __future__ import annotations

from homeassistant.const import Platform
from homeassistant.core import HomeAssistant

from .coordinator import BatteryCoordinator, JasmineConfigEntry, JasmineCoordinator, RuntimeData

PLATFORMS = [Platform.SENSOR, Platform.UPDATE]


async def async_setup_entry(hass: HomeAssistant, entry: JasmineConfigEntry) -> bool:
    """Set up the integration."""
    updates = JasmineCoordinator(hass, entry)
    battery = BatteryCoordinator(hass, entry)
    await updates.async_config_entry_first_refresh()
    await battery.async_refresh()
    entry.runtime_data = RuntimeData(updates, battery)
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: JasmineConfigEntry) -> bool:
    """Unload the integration."""
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
