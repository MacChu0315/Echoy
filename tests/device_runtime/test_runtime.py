"""Compile production device runtime with pthread RTOS and board substitutes.

No device access, firmware writes, physical sound or simulated UI backend.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
class DeviceRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='echoy-device-runtime-')
        cls.binary=Path(cls.temp.name)/'runtime-check'
        main=ROOT/'projects/echoy/main'
        command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-pthread',
                 '-DCONFIG_ECHOY_AUDIO_DIAGNOSTICS=1',
                 '-I'+str(HERE/'stubs'),'-I'+str(HERE),'-I'+str(main),
                 str(HERE/'test_runtime.c'),str(HERE/'fakes.c'),
                 *[str(main/(name+'.c')) for name in ['platform_device','echoy_band','echoy_model','echoy_music','echoy_arrange','echoy_audio','echoy_audio_probe','echoy_device_pcm']],
                 '-Wl,--wrap=fake_log','-Wl,--wrap=xSemaphoreTake','-Wl,--wrap=board_audio_write','-Wl,--wrap=echoy_band_create','-Wl,--wrap=echoy_band_prepare_job','-Wl,--wrap=echoy_band_calibrate_job','-lm','-o',str(cls.binary)]
        if os.environ.get('ECHOY_RUNTIME_SANITIZE')=='1':
            command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        subprocess.run(command,check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_case(self,name):
        result=subprocess.run([str(self.binary),name],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr)
        self.assertIn('PASS ',result.stdout)
    def test_link_snapshots_preserve_audio_owner_and_silence(self):self.run_case('link')
    def test_cache_unknown_battery_and_blocked_io(self):self.run_case('cache')
    def test_ui_reader_cannot_block_audio_or_pause(self):self.run_case('cache_contention_control')
    def test_link_reader_cannot_block_audio_or_pause(self):self.run_case('cache_contention_link')
    def test_record_cancel_fifo(self):self.run_case('fifo')
    def test_failed_calibration_cannot_play_uncalibrated_bank(self):self.run_case('calibration_failure')
    def test_pause_discards_delayed_pcm(self):self.run_case('limiter_pause')
    def test_overflow_fail_silent_and_recovery(self):self.run_case('overflow')
    def test_partial_write_and_recovery(self):self.run_case('partial')
    def test_empty_battery_survives_unknown_read(self):self.run_case('battery')
    def test_stop_during_startup_then_retry(self):self.run_case('startup')
    def test_stop_with_immutable_job_in_flight(self):self.run_case('prepare')
    def test_partial_task_creation(self):self.run_case('task_failure')
    def test_health_stop_retry(self):self.run_case('health_stop')
    def test_close_error_is_not_success(self):self.run_case('close_failure')
    def test_loading_cache_satisfies_gallery_contract(self):self.run_case('loading_cache')
    def test_initial_audio_open_failure_needs_recovery(self):self.run_case('open_failure')
    def test_flush_failure_stops_output(self):self.run_case('flush_failure')
    def test_late_output_faults_without_catchup(self):self.run_case('deadline')
    def test_deadline_trace_records_delayed_write_and_exact_lateness(self):self.run_case('deadline_trace')
    def test_audio_waits_for_explicit_activation(self):self.run_case('acceptance_gate')
    def test_shutdown_before_activation(self):self.run_case('stop_unactivated')
    def test_reference_tone_is_explicit_bounded_and_keeps_seed(self):self.run_case('probe')
    def test_reference_tone_ui_cancel(self):self.run_case('probe_cancel')
    def test_reference_tone_readback_failure(self):self.run_case('probe_failure')
    def test_reference_tone_partial_write(self):self.run_case('probe_partial')
if __name__=='__main__':unittest.main(verbosity=2)
