"""Battery sensors of the Mi A2, read from the fuel gauge."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import (
    PERCENTAGE,
    UnitOfElectricCurrent,
    UnitOfElectricPotential,
    UnitOfPower,
    UnitOfTemperature,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import BatteryCoordinator, JasmineConfigEntry


@dataclass(frozen=True, kw_only=True)
class BatteryDescription(SensorEntityDescription):
    """Sensor of the fuel gauge, key is the name of the value in the coordinator data."""

    scale: float = 1.0


SENSORS = (
    BatteryDescription(
        key="capacity",
        name="Battery",
        device_class=SensorDeviceClass.BATTERY,
        native_unit_of_measurement=PERCENTAGE,
        state_class=SensorStateClass.MEASUREMENT,
    ),
    BatteryDescription(
        key="voltage_now",
        name="Battery voltage",
        device_class=SensorDeviceClass.VOLTAGE,
        native_unit_of_measurement=UnitOfElectricPotential.VOLT,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=2,
    ),
    BatteryDescription(
        key="current_now",
        name="Battery current",
        device_class=SensorDeviceClass.CURRENT,
        native_unit_of_measurement=UnitOfElectricCurrent.MILLIAMPERE,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
        scale=1000.0,
    ),
    BatteryDescription(
        key="power",
        name="Battery power",
        device_class=SensorDeviceClass.POWER,
        native_unit_of_measurement=UnitOfPower.WATT,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=3,
    ),
    BatteryDescription(
        key="temp",
        name="Battery temperature",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: JasmineConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Set up the battery sensors."""
    coordinator = entry.runtime_data.battery
    async_add_entities(
        BatterySensor(coordinator, description)
        for description in SENSORS
        if description.key in (coordinator.data or {})
    )


class BatterySensor(CoordinatorEntity[BatteryCoordinator], SensorEntity):
    """One value of the fuel gauge."""

    entity_description: BatteryDescription
    _attr_has_entity_name = True
    _attr_device_info = DeviceInfo(identifiers={(DOMAIN, "jasmine")})

    def __init__(self, coordinator: BatteryCoordinator, description: BatteryDescription) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self.entity_description = description
        self._attr_unique_id = f"jasmine_battery_{description.key}"

    @property
    def native_value(self) -> float | None:
        """Value in the unit of the sensor."""
        value = self.coordinator.data.get(self.entity_description.key)
        if value is None:
            return None
        return round(value * self.entity_description.scale, 4)
