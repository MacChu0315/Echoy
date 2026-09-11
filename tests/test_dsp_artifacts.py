#!/usr/bin/env python3
"""Re-render every case and compare against measured Chromium RMS/peak fixtures."""
import array
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

reference=json.loads((Path(__file__).parent/'fixtures/audio/metrics.json').read_text())
with tempfile.TemporaryDirectory(prefix='echoy-dsp-') as directory:
    subprocess.run([sys.argv[1],directory],check=True,stdout=subprocess.DEVNULL)
    for case in reference['cases']:
        path=Path(directory)/(case['case']+'.wav')
        with wave.open(str(path),'rb') as f:
            assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(2,2,48000)
            pcm=array.array('h',f.readframes(f.getnframes()))
        if sys.byteorder!='little':pcm.byteswap()
        samples=pcm[::2]
        assert samples==pcm[1::2]
        rms=math.sqrt(sum(x*x for x in samples)/len(samples))/32767
        peak=max(abs(x) for x in samples)/32767
        browser=case['compressor_bypassed']
        assert abs(20*math.log10(rms/browser['rms']))<.5,(case['case'],'RMS',rms)
        assert abs(peak-browser['peak'])<.02,(case['case'],'peak',peak)
        first=case['scheduled_onsets_samples'][0]
        assert all(x==0 for x in samples[:first]),case['case']
        assert any(x for x in samples[first:first+2400]),case['case']
        assert max(abs(x) for x in samples[-4800:])<=2,case['case']
print('PASS DSP 12 phrases + 10 isolated voices/articulations, PCM/onset/tail and measured browser RMS/peak')
