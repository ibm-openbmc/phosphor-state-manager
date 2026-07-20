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
  ],
  "pcie_config": {
    "device_path": "/dev/bmc-device0",
    "redundancy_offset": "0x3EFFF80"
  }
}
```

## Fields

### Top Level

- **sibling_bmc_reset_gpio** (object, required): GPIO configuration for
  resetting the sibling BMC.
- **bmc_configs** (array, conditionally required): Array of BMC configuration
  objects. Required when there is a `sibling_bmc_reset_gpio`, optional when
  false.
- **pcie_config** (object, optional): PCIe storage configuration for redundancy
  data. If not present, PCIe storage functionality is disabled.

### BMC Config Object

- **bmc_pos** (number, required): Position/index of this BMC (0 or 1).
- **sibling_bmc_present_gpio** (object, required): GPIO configuration for
  detecting sibling BMC presence.

### GPIO Config Object (optional)

- **name** (string, required): Name of the GPIO line.
- **polarity** (string, required): Either "low" or "high" - indicates the active
  state of the GPIO.

### PCIe Config Object

- **device_path** (string, required): Path to the PCIe device (e.g.,
  '/dev/bmc-device0').
- **redundancy_offset** (string, required): Offset in the PCIe device for
  redundancy data as a hex string (e.g., '0x3EFFF80').

### Validating Config Files

Validate the config files with `validate_configs.py`.

```bash
# Validate all config files in the current directory
python3 validate_configs.py schema/schema.json .

# Validate a specific config file
python3 validate_configs.py schema/schema.json default.json
```
