"""Config flow, there is nothing to configure."""

from __future__ import annotations

from typing import Any

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult

from .const import DOMAIN


class JasmineConfigFlow(ConfigFlow, domain=DOMAIN):
    """Add the integration with one click."""

    VERSION = 1

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        """Confirm the setup."""
        if user_input is not None:
            return self.async_create_entry(title="Mi A2 HAOS updates", data={})
        return self.async_show_form(step_id="user")
