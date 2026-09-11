#!/usr/bin/env python3
"""Measure real C/Chromium artifacts; never substitute modeled Web Audio data."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np


def metrics(x):
    peak=float(np.max(np.abs(x)))
    audible=np.flatnonzero(np.abs(x)>1/32768)
    spectrum=np.abs(np.fft.rfft(x))**2
    frequency=np.fft.rfftfreq(len(x),1/48000)
    total=float(spectrum.sum())
    bands=[float(spectrum[(frequency>=lo)&(frequency<hi)].sum()/max(total,1e-30))
           for lo,hi in [(0,200),(200,2000),(2000,6000),(6000,24001)]]
    return dict(onset_sample=int(audible[0]) if len(audible) else None,
                last_audible_sample=int(audible[-1]) if len(audible) else None,
                rms=float(np.sqrt(np.mean(x*x))),peak=peak,
                spectral_centroid_hz=float((frequency*spectrum).sum()/max(total,1e-30)),
                band_energy_fraction=bands)


def compare(x,y):
    frame=960
    n=len(x)//frame*frame
    ex=np.sqrt(np.mean(x[:n].reshape(-1,frame)**2,axis=1))
    ey=np.sqrt(np.mean(y[:n].reshape(-1,frame)**2,axis=1))
    return dict(rms_delta_db=float(20*np.log10(max(np.linalg.norm(x),1e-30)/max(np.linalg.norm(y),1e-30))),
                waveform_nrmse=float(np.linalg.norm(x-y)/max(np.linalg.norm(y),1e-30)),
                envelope_20ms_nrmse=float(np.linalg.norm(ex-ey)/max(np.linalg.norm(ey),1e-30)))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    provenance=json.loads((args.directory/'reference-provenance.json').read_text())
    records=[]
    for specfile in sorted(args.directory.glob('*.json')):
        spec=json.loads(specfile.read_text())
        if 'events' not in spec:continue
        name=specfile.stem
        with wave.open(str(specfile.with_suffix('.wav')),'rb') as f:
            assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes())==(2,2,48000,spec['frames'])
            pcm=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,2)
            assert np.array_equal(pcm[:,0],pcm[:,1])
            c=pcm[:,0].astype(np.float64)/32767
        record={'case':name,'scheduled_onsets_samples':[round(e['when']*48000) for e in spec['events']],
                'c':metrics(c),'wav_sha256':hashlib.sha256(specfile.with_suffix('.wav').read_bytes()).hexdigest()}
        for suffix,key in [('web','original_compressor'),('web-dry','compressor_bypassed')]:
            browser=np.fromfile(args.directory/f'{name}.{suffix}.f32',dtype='<f4').astype(np.float64)
            assert len(browser)==len(c) and np.all(np.isfinite(browser))
            record[key]=metrics(browser)
            record[key]['comparison_to_c']=compare(c,browser)
            with wave.open(str(args.directory/f'{name}.{suffix}.wav'),'wb') as out:
                out.setnchannels(2);out.setsampwidth(2);out.setframerate(48000)
                quant=np.clip(np.rint(browser*32767),-32768,32767).astype('<i2')
                out.writeframes(np.repeat(quant[:,None],2,axis=1).tobytes())
        records.append(record)
    assert len(records)==22
    report={'provenance':provenance,'method':{'onset_threshold':'abs(sample)>1/32768',
        'envelope_window_frames':960,'spectral_bands_hz':[[0,200],[200,2000],[2000,6000],[6000,24000]],
        'spectrum':'whole-render rectangular FFT power; centroid and fractional energy',
        'c_quantization':'16-bit PCM divided by 32767; original Web measured float32',
        'compressor':'original path retained; bypass used only to isolate synthesis migration',
        'listening':'NOT PERFORMED; Task 10 human/headphone and physical ES8311 listening required'},'cases':records}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('case                      onset C/WebDry/Web  RMS dB C-WebDry  envelope error  waveform error')
    for r in records:
        c=r['compressor_bypassed']['comparison_to_c']
        print(f"{r['case']:25} {r['c']['onset_sample']:6}/{r['compressor_bypassed']['onset_sample']:6}/{r['original_compressor']['onset_sample']:6} {c['rms_delta_db']:15.3f} {c['envelope_20ms_nrmse']:15.5f} {c['waveform_nrmse']:15.5f}")

if __name__=='__main__':main()
