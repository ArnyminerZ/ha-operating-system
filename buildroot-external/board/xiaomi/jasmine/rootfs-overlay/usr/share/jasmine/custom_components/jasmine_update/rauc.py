"""Talk to RAUC over the system D-Bus, Core has /run/dbus mounted."""

from __future__ import annotations

import asyncio
from collections.abc import Callable

from dbus_fast import BusType
from dbus_fast.aio import MessageBus

from homeassistant.exceptions import HomeAssistantError

SERVICE = "de.pengutronix.rauc"
PATH = "/"
INTERFACE = "de.pengutronix.rauc.Installer"


async def _installer() -> tuple[MessageBus, object]:
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        introspection = await bus.introspect(SERVICE, PATH)
        proxy = bus.get_proxy_object(SERVICE, PATH, introspection)
        return bus, proxy.get_interface(INTERFACE)
    except Exception:
        bus.disconnect()
        raise


async def get_boot_slot() -> str:
    """Return the bootname (A or B) of the slot the system runs from."""
    bus, installer = await _installer()
    try:
        return await installer.get_boot_slot()
    finally:
        bus.disconnect()


async def install_bundle(host_path: str, on_progress: Callable[[int], None]) -> None:
    """Install a bundle that is visible to the host as host_path."""
    bus, installer = await _installer()
    done: asyncio.Future[int] = asyncio.get_running_loop().create_future()

    def completed(result: int) -> None:
        if not done.done():
            done.set_result(result)

    installer.on_completed(completed)
    try:
        await installer.call_install_bundle(host_path, {})
        while not done.done():
            percent, _message, _depth = await installer.get_progress()
            on_progress(percent)
            await asyncio.wait({done}, timeout=2)
        if done.result() != 0:
            raise HomeAssistantError(f"RAUC failed: {await installer.get_last_error()}")
    finally:
        bus.disconnect()
