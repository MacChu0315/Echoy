"""Run the production app_main with isolated board/GSP lifecycle substitutes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class Startup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="echoy-startup-")
        directory = Path(cls.temp.name)
        # Copy byte-for-byte so quoted application headers resolve to the fakes;
        # the actual startup function and callback remain the production code.
        shutil.copyfile(ROOT / "projects/echoy/main/main.c", directory / "main.c")
        shutil.copyfile(HERE / "fakes.h", directory / "fakes.h")
        headers = (
            "board_display.h board_touch_trace.h bsp/esp_mosaico.h echoy_ui.h "
            "esp_gsp_esp_lcd.h esp_iris.h esp_log.h esp_app_desc.h iris_ota_support.h "
            "iris_screen_mirror.h nvs_flash.h platform_device.h echoy_audio_diagnostics.h "
            "echoy_snapshot_diagnostics.h echoy_measurement_diagnostics.h sdkconfig.h ui_bundle.h"
        )
        for header in headers.split():
            path = directory / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "fakes.h"\n')
        cls.binary = directory / "startup"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                        "-I" + str(directory), str(directory / "main.c"), str(HERE / "fakes.c"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_startup_and_manual_brightness(self):
        self.check_case("normal")

    def test_initial_brightness_failure_stops_before_render(self):
        self.check_case("initial_failure")

    def test_radio_failure_keeps_audio_inactive(self):
        self.check_case("radio_failure")

    def test_health_failure_keeps_audio_inactive(self):
        self.check_case("health_failure")

    def test_inherited_motor_is_off_even_when_ui_bundle_is_invalid(self):
        self.check_case("bundle_failure")

    def test_motor_shutdown_failure_keeps_recovery_and_stops_app(self):
        self.check_case("motor_failure")

    def check_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
