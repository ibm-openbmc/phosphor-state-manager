#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Validate redundant BMC configuration files against JSON schema.

Usage:
    validate_configs.py <schema_file> <config_file_or_directory>
"""

import argparse
import json
import sys
from pathlib import Path
from typing import List, Tuple

try:
    from jsonschema import Draft7Validator
except ImportError:
    print(
        "Error: jsonschema module not found. Install with: pip install jsonschema",
        file=sys.stderr,
    )
    sys.exit(1)


def load_json_file(filepath: Path) -> dict:
    """Load and parse a JSON file."""
    try:
        with open(filepath, "r") as f:
            return json.load(f)
    except json.JSONDecodeError as e:
        raise ValueError(f"Invalid JSON in {filepath}: {e}")
    except Exception as e:
        raise ValueError(f"Failed to read {filepath}: {e}")


def validate_config(config_path: Path, schema: dict) -> Tuple[bool, str]:
    """
    Validate a single config file against the schema.

    Returns:
        Tuple of (is_valid, error_message)
    """
    try:
        config = load_json_file(config_path)
        validator = Draft7Validator(schema)
        errors = list(validator.iter_errors(config))

        if errors:
            error_messages = []
            for error in errors:
                path = (
                    ".".join(str(p) for p in error.path)
                    if error.path
                    else "root"
                )
                error_messages.append(f"  - At '{path}': {error.message}")
            return False, "\n".join(error_messages)

        return True, ""
    except Exception as e:
        return False, f"  - {str(e)}"


def find_config_files(path: Path) -> List[Path]:
    """Find all JSON config files in the given path."""
    if path.is_file():
        if path.suffix == ".json":
            return [path]
        return []

    # Directory - find all .json files (excluding subdirectories)
    config_files = []
    for json_file in path.glob("*.json"):
        if json_file.is_file():
            config_files.append(json_file)
    return sorted(config_files)


def main():
    parser = argparse.ArgumentParser(
        description="Validate redundant BMC configuration files against JSON schema"
    )
    parser.add_argument(
        "schema", type=Path, help="Path to the JSON schema file"
    )
    parser.add_argument(
        "config",
        type=Path,
        help="Path to config file or directory containing config files",
    )

    args = parser.parse_args()

    # Load schema
    if not args.schema.exists():
        print(f"Error: Schema file not found: {args.schema}", file=sys.stderr)
        return 1

    try:
        schema = load_json_file(args.schema)
    except ValueError as e:
        print(f"Error loading schema: {e}", file=sys.stderr)
        return 1

    # Find config files
    if not args.config.exists():
        print(f"Error: Config path not found: {args.config}", file=sys.stderr)
        return 1

    config_files = find_config_files(args.config)
    if not config_files:
        print(
            f"Error: No config files found in {args.config}", file=sys.stderr
        )
        return 1

    all_valid = True
    for config_file in config_files:
        is_valid, error_msg = validate_config(config_file, schema)

        if is_valid:
            print(f"{config_file.name}: VALID")
        else:
            print(f"{config_file.name}: INVALID")
            print(error_msg)
            all_valid = False

    return 0 if all_valid else 1


if __name__ == "__main__":
    sys.exit(main())
