# Redundant BMC Configuration Files

This directory contains the platform-specific redundant BMC configuration files.

A config file is required, and the one to use is specified with the
`rbmc-config-file` meson option.

It is then installed into
`/usr/share/phosphor-state-manager/redundant-bmc/config.json`.

## Config File Format

```json
{
  "sibling_bmc_reset_gpio": {
    "name": "sibling-bmc-reset-n",
    "polarity": "low"
  },
  "bmc_configs": [
    {
      "bmc_pos": 0,
      "sibling_bmc_present_gpio": {
        "name": "chassis2-present",
        "polarity": "low"
      }
    },
    {
      "bmc_pos": 1,
      "sibling_bmc_present_gpio": {
        "name": "chassis1-present",
        "polarity": "high"
      }
    }
  ]
}
```

## Fields

### Top Level

- **sibling_bmc_reset_gpio** (object, required): GPIO configuration for
  resetting the sibling BMC.
- **bmc_configs** (array, conditionally required): Array of BMC configuration
  objects. Required when there is a `sibling_bmc_reset_gpio`, optional when
  false.

### BMC Config Object

- **bmc_pos** (number, required): Position/index of this BMC (0 or 1).
- **sibling_bmc_present_gpio** (object, required): GPIO configuration for
  detecting sibling BMC presence.

### GPIO Config Object (optional)

- **name** (string, required): Name of the GPIO line.
- **polarity** (string, required): Either "low" or "high" - indicates the active
  state of the GPIO.
