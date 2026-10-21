"""Check an Echoy Iris 0.2 candidate offline with its pinned official parser."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    if not __debug__:
        raise SystemExit("Validation requires Python assertions: unset PYTHONOPTIMIZE and do not use -O.")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--utils", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(args.utils.resolve() / "ESP-Iris/components/esp_iris/tools"))
    from iris_gateway.system_update import load_system_update_bundle
    from iris_gateway.firmware import inspect_firmware_image

    bundle = load_system_update_bundle(args.bundle)
    assert bundle.manifest["schema"] == "esp-iris-system-update/0.2"
    assert bundle.chip_id == 32 and bundle.flash_size == 16 * 1024 * 1024
    assert not bundle.signature and not bundle.key_id
    assert bundle.manifest["minimum_recovery_version"] == "0.2.0"
    config = (args.build_dir / "sdkconfig").read_text()
    for setting in (
        'CONFIG_ESP_IRIS_PRODUCT_CONTRACT="esp-mosaico/v2"',
        'CONFIG_ESP_IRIS_LAYOUT_ID="mosaico-retained-test-2m-v2"',
        "CONFIG_ESP_IRIS_RECOVERY_ABI=2", "CONFIG_TINYUSB_CDC_COUNT=2",
        "CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y",
        "# CONFIG_ESP_IRIS_OTA is not set",
        "# CONFIG_ESP_MOSAICO_APP_AUTO_ACCEPT is not set",
        "CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y", "CONFIG_ECHOY_PHRASE_PICKUP=y",
        "# CONFIG_ECHOY_OUTPUT_MEASUREMENT is not set",
    ):
        assert setting in config, setting
    version = re.search(r'^CONFIG_APP_PROJECT_VER="([^"]+)"$', config, re.M).group(1)
    assert bundle.manifest["release"] == version
    components = {c.kind.name.lower(): c for c in bundle.components}
    assert len(bundle.components) == 3 and set(components) == {"application", "data", "partition_table"}
    for kind, offset, filename in (("application", 0x210000, "echoy.bin"),
                                   ("data", 0xf00000, "ui_apps.bin"),
                                   ("partition_table", 0x8000, "partition_table/partition-table.bin")):
        component = components[kind]
        assert component.flags == 0 and component.target_offset == offset
        image = (args.build_dir / filename).read_bytes()
        if kind == "partition_table":
            # IDF emits 3 KiB; the official bundle pads the entire 4 KiB
            # partition-table sector with erased bytes before hashing/writing.
            assert len(image) == 3072 and component.size == 4096
            image = image.ljust(4096, b"\xff")
        assert component.data == image, filename
    assert components["partition_table"].sha256.hex() == bundle.target_layout_sha256
    # Read the actual binary table, including the retained region excluded from
    # the update components. Normal application updates must not replace it.
    description = json.loads((args.build_dir / "project_description.json").read_text())
    iris_component = args.utils.resolve() / "ESP-Iris/components/esp_iris"
    assert iris_component in {Path(p).resolve() for p in description["build_component_paths"]}
    cache = (args.build_dir / "CMakeCache.txt").read_text()
    idf_python = re.search(r"^PYTHON:[^=]+=(.+)$", cache, re.M).group(1)
    # The IDF parser has dependencies in IDF's own Python environment, not in
    # the separate Iris host environment used to inspect the update archive.
    code = """import json, pathlib, sys
sys.path.insert(0, sys.argv[1])
import gen_esp32part
table = gen_esp32part.PartitionTable.from_binary(pathlib.Path(sys.argv[2]).read_bytes())
print(json.dumps({p.name: [p.type, p.subtype, p.offset, p.size] for p in table}))
"""
    table = json.loads(subprocess.check_output([
        idf_python, "-c", code,
        str(Path(description["idf_path"]) / "components/partition_table"),
        str(args.build_dir / "partition_table/partition-table.bin"),
    ], text=True))
    assert "factory" not in table
    for name, subtype, offset, size in (("vibe_mode", 0x20, 0x20000, 0x1c0000),
                                        ("main_app", 0x10, 0x210000, 0xcf0000)):
        assert table[name] == [0, subtype, offset, size]
    assert table["nvs"][2:] == [0x200000, 0x10000]
    assert table["ui_apps"][2:] == [0xf00000, 0x100000]
    firmware = inspect_firmware_image(components["application"].data)
    assert firmware.project_name == "echoy" and firmware.version == version
    assert hashlib.sha256((args.build_dir / "echoy.elf").read_bytes()).hexdigest() == firmware.elf_sha256
    scene = json.loads((root / "projects/echoy/ui/main.json").read_text())
    labels = [o for o in scene["objects"] if o.get("bind") in {"info_value_version", "info_value_version_en"}]
    assert len(labels) == 2 and all(o["text"] == version for o in labels)
    print(json.dumps({"passed": True, "release": version,
                      "schema": bundle.manifest["schema"],
                      "bundle_sha256": hashlib.sha256(args.bundle.read_bytes()).hexdigest(),
                      "elf_sha256": firmware.elf_sha256,
                      "scope": "offline artifact/configuration/partition checks; no device or platform acceptance"}, indent=2))


if __name__ == "__main__":
    main()
