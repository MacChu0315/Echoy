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
                 '-DCONFIG_ECHOY_OUTPUT_MEASUREMENT=1',
                 '-DCONFIG_ECHOY_HARMONY_BASS_TRIAL=0',
                 '-I'+str(HERE/'stubs'),'-I'+str(HERE),'-I'+str(main),
                 str(HERE/'test_runtime.c'),str(HERE/'fakes.c'),
                 *[str(main/(name+'.c')) for name in ['platform_device','echoy_band','echoy_model','echoy_music','echoy_arrange','echoy_audio','echoy_audio_probe','echoy_device_pcm','echoy_output_measurement']],
                 '-Wl,--wrap=echoy_output_measurement_analyze',
                 '-Wl,--wrap=echoy_band_action',
                 '-Wl,--wrap=echoy_audio_cut_harmony_bass',
                 '-Wl,--wrap=fake_log','-Wl,--wrap=xSemaphoreTake','-Wl,--wrap=board_audio_write','-Wl,--wrap=echoy_band_create','-Wl,--wrap=echoy_band_prepare_job','-Wl,--wrap=echoy_band_calibrate_job','-lm','-o',str(cls.binary)]
        if os.environ.get('ECHOY_RUNTIME_SANITIZE')=='1':
            command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        subprocess.run(command,check=True)
        cls.normal_binary=Path(cls.temp.name)/'runtime-normal-check'
        normal=[arg.replace('-DCONFIG_ECHOY_OUTPUT_MEASUREMENT=1','-DCONFIG_ECHOY_OUTPUT_MEASUREMENT=0')
                for arg in command]
        normal[-1]=str(cls.normal_binary)
        subprocess.run(normal,check=True)
        cls.trial_binary=Path(cls.temp.name)/'runtime-harmony-trial-check'
        trial=[arg.replace('-DCONFIG_ECHOY_HARMONY_BASS_TRIAL=0','-DCONFIG_ECHOY_HARMONY_BASS_TRIAL=1')
               for arg in normal]
        trial[-1]=str(cls.trial_binary)
        subprocess.run(trial,check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_case(self,name,binary=None):
        binaries=[binary] if binary else [self.binary]
        if binary is None and not name.startswith('measure_'):
            binaries += [self.normal_binary,self.trial_binary]
        for target in binaries:
            result=subprocess.run([str(target),name],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,str(target)+'\n'+result.stdout+'\n'+result.stderr)
            self.assertIn('PASS ',result.stdout)
    def test_link_snapshots_preserve_audio_owner_and_silence(self):self.run_case('link')
    def test_retained_image_storage_is_psram_without_fallback(self):self.run_case('image_storage')
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
    def test_measurement_explicit_bounded_and_budget(self):self.run_case('measure_complete')
    def test_measurement_cancel_fifo_and_any_ui_action(self):self.run_case('measure_cancel')
    def test_measurement_worker_shutdown_race(self):self.run_case('measure_analysis_stop')
    def test_measurement_cancelled_analysis_cannot_publish_stale_result(self):self.run_case('measure_analysis_cancel')
    def test_measurement_reader_cannot_block_capture_or_cancel(self):self.run_case('measure_reader')
    def test_measurement_shutdown_during_capture(self):self.run_case('measure_capture_stop')
    def test_measurement_requires_psram(self):self.run_case('measure_alloc_failure')
    def test_measurement_cancel_at_result_publication(self):self.run_case('measure_publish_cancel')
    def test_measurement_failed_stop_erases_raw_capture(self):
        for case in ('close','health'):
            with self.subTest(case=case):self.run_case('measure_stop_'+case)
    def test_normal_build_cannot_open_microphone(self):
        result=subprocess.run([str(self.normal_binary),'measure_disabled'],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr)
    def test_measurement_cancel_overflow_stops(self):self.run_case('measure_overflow')
    def test_harmony_trial_disabled_in_ordinary_build(self):self.run_case('harmony_disabled',self.normal_binary)
    def test_harmony_trial_stays_on_worker_and_paused(self):self.run_case('harmony_worker',self.trial_binary)
    def test_harmony_trial_failure_keeps_old_valid_bank(self):self.run_case('harmony_failure',self.trial_binary)
    def test_harmony_trial_stale_job_cannot_play(self):self.run_case('harmony_stale',self.trial_binary)
    def test_measurement_failures(self):
        for case in ('open','flush','read','partial_read','partial_write','partial_read_ok',
                     'partial_write_ok','deadline','close','reopen'):
            with self.subTest(case=case):self.run_case('measure_fault_'+case)
if __name__=='__main__':unittest.main(verbosity=2)
