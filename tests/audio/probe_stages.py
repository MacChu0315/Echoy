"""Observe the actual C synthesis/PCM stages; never plays or changes devices.

Requires the same NumPy and C compiler as compare.py. All generated files stay
in the requested output directory. WAV comparisons are peak-safe and matched
to a common RMS; exact amplitudes remain in f32/s16 files and report.json.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "projects/echoy/main"
RATE = 24000


def numeric(value, integer=False):
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("plan fields must be finite numbers")
    if integer and int(value) != value:
        raise ValueError("integer plan field has a fractional value")
    return str(int(value)) if integer else repr(float(value))


def initializer(plan):
    """Accept the native runner's exact music JSON, including linked events."""
    plan = plan.get("music", plan)
    source, events = plan["source"], plan["events"]
    if not 0 < len(source) <= 16 or not 0 < len(events) <= 64:
        raise ValueError("plan requires 1..16 source notes and 1..64 events")

    def note(n, seed=False):
        fields = [f'.{key}={numeric(n[key], key == "kind")}'
                  for key in ("step", "velocity", "kind")]
        if not seed:
            for key in ("duration", "max_duration", "expression", "layer", "articulation", "note"):
                fields.append(f'.{key}={numeric(n.get(key, 0), key in ("layer", "articulation", "note"))}')
            step = n.get("source_step")
            fields += [f'.source_step={numeric(step if step is not None else 0)}',
                       f'.has_source_step={int(n.get("has_source_step", step is not None))}']
            for key, count in (("notes", "note_count"), ("harmony", "harmony_count")):
                values = n.get(key, [])
                if len(values) > 3:
                    raise ValueError("at most three chord pitches")
                fields += [f'.{key}={{' + ",".join(numeric(x, True) for x in values or [0]) + "}",
                           f'.{count}={numeric(n.get(count, len(values)), True)}']
        return "{" + ",".join(fields) + "}"

    fields = [f'.{key}={numeric(plan[key], key in ("role", "phrase"))}'
              for key in ("role", "phrase", "density", "duration")]
    fields += [f'.source_count={len(source)}', f'.count={len(events)}',
               '.source={' + ",".join(note(n, True) for n in source) + "}",
               '.events={' + ",".join(note(n) for n in events) + "}"]
    return "{" + ",".join(fields) + "}"


def signal_metrics(data):
    data = np.asarray(data, dtype=float)
    if not np.all(np.isfinite(data)):
        raise ValueError("nonfinite observed samples")
    peak = float(np.max(np.abs(data)))
    rms = float(np.sqrt(np.mean(data * data)))
    spectrum = np.abs(np.fft.rfft(data)) ** 2
    hz = np.fft.rfftfreq(len(data), 1 / RATE)
    bands = {}
    for low, high in ((0, 120), (120, 300), (300, 1000), (1000, 3000), (3000, 12001)):
        bands[f"{low}-{high}_hz"] = float(np.sum(spectrum[(hz >= low) & (hz < high)]) /
                                         max(float(np.sum(spectrum)), 1e-100))
    return dict(peak=peak, rms=rms, mean=float(np.mean(data)),
                crest_db=20 * math.log10(max(peak, 1e-100) / max(rms, 1e-100)),
                maximum_adjacent_jump=float(np.max(np.abs(np.diff(data)))), energy_fractions=bands)


def shape_residual(reference, observed):
    """Difference after a best-fit constant gain, not a THD measurement."""
    gain = float(np.dot(reference, observed) / max(np.dot(reference, reference), 1e-100))
    residual = observed - reference * gain
    relative = float(np.linalg.norm(residual) / max(np.linalg.norm(observed), 1e-100))
    return dict(best_constant_gain=gain, relative_residual=relative,
                residual_db=20 * math.log10(max(relative, 1e-100)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument("--plan-bin", type=Path, help="complete canonical ECHP plan")
    inputs.add_argument("--plan-json", type=Path, help="native music JSON or {music: ...}")
    parser.add_argument("--seed", type=lambda s: int(s, 16), default=0x40AA)
    parser.add_argument("--role", type=int, choices=range(4), default=3)
    parser.add_argument("--phrase", type=int, choices=range(1, 4), default=2)
    parser.add_argument("--density", type=float, default=.5)
    parser.add_argument("--volume", type=float, default=.34)
    parser.add_argument("--original-pulse1", action="store_true",
                        help="set only for an unlinked local Pulse phrase 1")
    args = parser.parse_args()
    if not 0 <= args.volume <= 1 or not 0 <= args.density <= 1 or not 0 <= args.seed <= 65535:
        parser.error("volume/density must be 0..1 and seed must be 0000..ffff")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error("output directory must be empty (preserve previous evidence)")
    build = output / "build"
    build.mkdir()
    if args.plan_bin:
        payload = args.plan_bin.read_bytes()
        if not 32 <= len(payload) <= 8192:
            parser.error("invalid canonical plan length")
        body = "static const uint8_t bytes[]={" + ",".join(map(str, payload)) + "};\n"
        body += "return echoy_link_plan_decode(bytes, sizeof(bytes), plan);"
        provenance = dict(kind="canonical_echp", path=str(args.plan_bin.resolve()))
    elif args.plan_json:
        body = "*plan=(echoy_plan_t)" + initializer(json.loads(args.plan_json.read_text())) + ";return true;"
        provenance = dict(kind="native_plan_json", path=str(args.plan_json.resolve()))
    else:
        body = (f"return echoy_compose_mask({args.seed}, {args.role}, {args.phrase}, "
                f"{numeric(args.density)}, plan);")
        provenance = dict(kind="standalone_composed", seed=f"{args.seed:04x}")
    (build / "probe_input.h").write_text(
        f"#define PROBE_ORIGINAL_PULSE1 {int(args.original_pulse1)}\n"
        "static bool probe_load_plan(echoy_plan_t *plan) {" + body + "}\n")
    original_source = (MAIN / "echoy_audio.c").read_text()
    marker = "    compress(output, frames, work->sine);"
    if original_source.count(marker) != 1:
        raise RuntimeError("synthesis observer insertion point changed; inspect before adapting")
    observed_source = original_source.replace(marker,
        "    probe_synth_stage(0, output, frames);\n" + marker +
        "\n    probe_synth_stage(1, output, frames);")
    normalization_marker = "    double rms[3], peak[3], target = .045;"
    if original_source.count(normalization_marker) != 1:
        raise RuntimeError("normalization observer insertion point changed; inspect before adapting")
    observed_source = observed_source.replace(normalization_marker,
        "    probe_synth_stage(2, loops->samples[ECHOY_AUDIO_FULL], loops->frames);\n" + normalization_marker)
    observed_source = "#include <stddef.h>\nvoid probe_synth_stage(unsigned, const float *, size_t);\n" + observed_source
    (build / "observed_audio.c").write_text(observed_source)
    command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(os.environ.get("AUDIO_TEST_CFLAGS", ""))
    command += ["-I" + str(MAIN), "-I" + str(build), str(ROOT / "tests/audio/stages_probe.c"),
                str(MAIN / "echoy_music.c"), str(MAIN / "echoy_device_pcm.c"),
                str(MAIN / "echoy_link_codec.c")]
    results = []
    for name, source in (("observed", build / "observed_audio.c"), ("production", MAIN / "echoy_audio.c")):
        destination = output if name == "observed" else build / "production"
        destination.mkdir(exist_ok=True)
        binary = build / (name + "_probe")
        subprocess.run(command + [str(source), "-lm", "-o", str(binary)], check=True)
        result = subprocess.run([str(binary), str(destination), str(args.volume)],
                                check=True, capture_output=True, text=True)
        results.append(json.loads(result.stdout))
    for name in ("plan.echp", "normalized_full.f32", "input.f32", "hp_drive.f32", "limiter_gain.f32", "pcm.s16"):
        if (output / name).read_bytes() != (build / "production" / name).read_bytes():
            raise RuntimeError(f"observer changed production output: {name}")
    if results[0] != results[1]:
        raise RuntimeError("observer changed production metrics")
    meta = results[0]
    frames, lookahead = meta["loop_frames"], meta["lookahead_frames"]
    signals = {name: np.fromfile(output / (name + ".f32"), dtype="<f4").astype(float)
               for name in ("pre_compressor", "post_compressor", "post_fade", "normalized_full", "input", "hp_drive", "limiter_gain")}
    signals["pcm"] = np.fromfile(output / "pcm.s16", dtype="<i2").astype(float) / 32767
    measured = {name: x[frames:] if len(x) > frames else x for name, x in signals.items()
                if name != "limiter_gain"}
    delayed = signals["hp_drive"][frames - lookahead:-lookahead]
    gain = signals["limiter_gain"][frames:]
    # -80 dBFS at the actual delayed limiter input; meaningful thresholds are
    # gain reduction > .1 / 1 / 3 / 6 dB, not float gain < .99999 on denormals.
    audible = np.abs(delayed) >= 1e-4
    reduction = -20 * np.log10(np.maximum(gain[audible], 1e-100))
    effective = dict(input_threshold=1e-4, active_fraction=float(np.mean(audible)),
                     gain_reduction_db_percentiles={str(p): float(np.percentile(reduction, p)) if len(reduction) else 0
                                                   for p in (0, 50, 95, 100)},
                     active_reduction_fraction={str(db): float(np.mean(reduction > db)) if len(reduction) else 0
                                                for db in (.1, 1, 3, 6)})
    boundaries = {}
    for name in ("input", "hp_drive", "pcm"):
        data = signals[name]
        boundaries[name] = [{"frame": at, "jump": float(abs(data[at] - data[at - 1])),
                             "local_peak_1ms": float(np.max(np.abs(data[at - 24:at + 24])))}
                            for at in (frames, 2 * frames)]
    # The shared compressor has a 144-frame delay. Compare before the separate
    # end fade and normalization; bank start/end are measured independently.
    shared = shape_residual(signals["pre_compressor"][:-144], signals["post_compressor"][144:])
    normalization = shape_residual(signals["post_fade"], signals["normalized_full"])
    limiter = shape_residual(delayed, measured["pcm"])
    # Every audition WAV has the same RMS. If any crest factor needs more
    # headroom, lower the common target for all files rather than clip one.
    metrics = {name: signal_metrics(x) for name, x in measured.items()}
    target = min([10 ** (-24 / 20)] + [.8 * m["rms"] / m["peak"]
                                      for m in metrics.values() if m["peak"] > 0])
    wav_gains = {}
    for name, data in measured.items():
        scale = target / max(metrics[name]["rms"], 1e-100)
        wav_gains[name] = scale
        with wave.open(str(output / (name + "_matched.wav")), "wb") as wav:
            wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            wav.writeframes(np.rint(data * scale * 32767).astype("<i2").tobytes())
    report = dict(input=provenance, volume=args.volume, production=meta,
                  verification=dict(observer_bit_identical=True, block_partition_bit_identical=True),
                  sample_rate=RATE, warmup_frames=frames, measured_frames=frames * 2,
                  stages=metrics, effective_limiter=effective, boundaries=boundaries,
                  shared_compressor_shape_residual=shared, normalization_shape_residual=normalization,
                  limiter_shape_residual=limiter,
                  matched_wavs=dict(common_rms=target, gains=wav_gains),
                  source_sha256={name: hashlib.sha256((MAIN / name).read_bytes()).hexdigest()
                                 for name in ("echoy_audio.c", "echoy_device_pcm.c", "echoy_music.c", "echoy_link_codec.c")},
                  plan_sha256=hashlib.sha256((output / "plan.echp").read_bytes()).hexdigest(),
                  limits=["Steady single-tile FULL bank replay; no transitions, cues, multi-tile sum or device timing",
                          "Shape residual includes intended nonlinear dynamics; it is not THD or listening acceptance",
                          "Spectral energy and adjacent-sample jumps do not identify an audible click or analog fault",
                          "Native JSON events/canonical ECHP preserve linked pitches; a seed alone does not"])
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(dict(report=str(output / "report.json"), production=meta,
                          effective_limiter=effective, limiter_shape_residual=limiter), indent=2))


if __name__ == "__main__":
    main()
