#include "echoy_audio.h"
#include "echoy_device_pcm.h"
#include "echoy_link_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N ECHOY_AUDIO_LOOP_FRAMES
static float saved[3][N];
static int fail_after = -1, allocations;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {
    if (fail_after >= 0 && allocations++ >= fail_after) return NULL;
    return __real_malloc(n);
}
static void save(const echoy_audio_loops_t *a) {
    for(unsigned k=0;k<3;k++)memcpy(saved[k],a->samples[k],sizeof saved[k]);
}
static void unchanged(const echoy_audio_loops_t *a) {
    for(unsigned k=0;k<3;k++)assert(!memcmp(saved[k],a->samples[k],sizeof saved[k]));
}
static echoy_audio_loops_t *prepare(echoy_plan_t *p,unsigned role,unsigned phrase,unsigned seed) {
    assert(echoy_compose_mask(seed,role,phrase,.5,p));
    echoy_audio_loops_t *a=NULL;
    assert(echoy_audio_prepare(p,role==ECHOY_PULSE&&phrase==1,&a)==ECHOY_AUDIO_OK);
    return a;
}
/* Fails if the optional path is ignored, mutates a reference bank/shared target,
 * leaks mode into later prepares, or changes the musical plan. */
static void isolated_profile(void) {
    const unsigned seeds[]={0x1085,0x40aa,0x8534};
    for(unsigned s=0;s<3;s++)for(unsigned role=0;role<4;role++)for(unsigned phrase=1;phrase<=3;phrase++){
        echoy_plan_t p;echoy_audio_loops_t *a=prepare(&p,role,phrase,seeds[s]);
        echoy_plan_t before=p;double target=a->target_rms;save(a);
        assert(echoy_audio_voice_for_speaker(&p,a));
        assert(!memcmp(&p,&before,sizeof p));assert(a->target_rms==target);
        for(unsigned k=0;k<2;k++)assert(!memcmp(saved[k],a->samples[k],sizeof saved[k]));
        if(role==ECHOY_DUST)unchanged(a);
        else assert(memcmp(saved[ECHOY_AUDIO_FULL],a->samples[ECHOY_AUDIO_FULL],sizeof saved[0]));
        double energy=0;
        for(unsigned i=0;i<N;i++){
            float sample=a->samples[ECHOY_AUDIO_FULL][i];assert(isfinite(sample));
            energy+=(double)sample*sample;
        }
        assert(energy/N>1e-6 && energy/N<.01);
        if(role!=ECHOY_HARMONY)assert(a->samples[ECHOY_AUDIO_FULL][N-1]==0);
        echoy_audio_loops_t *again=NULL;
        assert(echoy_audio_prepare(&p,role==ECHOY_PULSE&&phrase==1,&again)==ECHOY_AUDIO_OK);
        unchanged(again);echoy_audio_free_loops(again);echoy_audio_free_loops(a);
    }
}
/* Fails on partial publication, lost old bank on allocation failure or accidental
 * allocation of Dust's unchanged waveform. */
static void allocation_failure_is_atomic(void) {
    for(unsigned role=0;role<4;role++){
        echoy_plan_t p;echoy_audio_loops_t *a=prepare(&p,role,2,0x8534);save(a);
        allocations=0;fail_after=0;
        assert(echoy_audio_voice_for_speaker(&p,a)==(role==ECHOY_DUST));
        fail_after=-1;unchanged(a);
        if(role==ECHOY_DUST)assert(allocations==0);
        else assert(echoy_audio_voice_for_speaker(&p,a));
        echoy_audio_free_loops(a);
    }
}
/* Fails if malformed inputs can overwrite an owned prepared buffer. */
static void bad_input_is_atomic(void) {
    echoy_plan_t p;echoy_audio_loops_t *a=prepare(&p,ECHOY_HARMONY,2,0x8534);save(a);
    assert(!echoy_audio_voice_for_speaker(NULL,a));assert(!echoy_audio_voice_for_speaker(&p,NULL));
    a->frames=N-1;assert(!echoy_audio_voice_for_speaker(&p,a));a->frames=N;
    double target=a->target_rms;a->target_rms=NAN;assert(!echoy_audio_voice_for_speaker(&p,a));a->target_rms=target;
    echoy_plan_t bad=p;bad.events[bad.count-1].duration=NAN;
    assert(!echoy_audio_voice_for_speaker(&bad,a));
    float *full=a->samples[2];a->samples[2]=a->samples[0]+1;
    assert(!echoy_audio_voice_for_speaker(&p,a));a->samples[2]=full;
    unchanged(a);
    a->samples[2][N-1]=NAN;save(a);assert(!echoy_audio_voice_for_speaker(&p,a));unchanged(a);
    echoy_audio_free_loops(a);
}
/* MIDI edge inputs catch unchecked high-harmonic phase/alias branches and
 * near-zero normalization. Their bank must stay finite or fail atomically. */
static void legal_pitch_edges(void) {
    const int pitches[]={0,19,20,69,113,114,127};
    for(unsigned role=ECHOY_VOICE;role<=ECHOY_HARMONY;role++)for(unsigned n=0;n<7;n++){
        echoy_plan_t p;assert(echoy_compose_mask(0x1048,role,2,.5,&p));
        for(size_t i=0;i<p.count;i++){
            p.events[i].note=pitches[n];
            for(unsigned j=0;j<p.events[i].note_count;j++)p.events[i].notes[j]=pitches[n];
        }
        echoy_audio_loops_t *a=NULL;assert(echoy_audio_prepare(&p,false,&a)==ECHOY_AUDIO_OK);
        assert(echoy_audio_voice_for_speaker(&p,a));
        for(unsigned i=0;i<N;i++)assert(isfinite(a->samples[2][i]));
        echoy_audio_free_loops(a);
    }
}
static double frequency_energy(const float *samples,double hz) {
    double real=0,imag=0;
    for(unsigned i=0;i<N;i++){
        double phase=6.2831853071795864769*hz*i/ECHOY_AUDIO_SAMPLE_RATE;
        real+=samples[i]*cos(phase);imag+=samples[i]*sin(phase);
    }
    return real*real+imag*imag;
}
/* A single known A4 note must retain its fundamental and add useful upper
 * harmonics, rather than merely rescale, silence or replace the musical note.
 * Frequency probes are independent of the renderer's oscillator/table. */
static void preserves_note_with_upper_harmonics(void) {
    for(unsigned role=ECHOY_VOICE;role<=ECHOY_HARMONY;role+=2){
        echoy_plan_t p;assert(echoy_compose_mask(0x1048,role,2,.5,&p));
        p.count=p.source_count=1;
        p.source[0]=(echoy_seed_note_t){.step=2,.kind=ECHOY_KICK,.velocity=.7};
        p.events[0]=(echoy_note_t){.step=2,.kind=role==ECHOY_VOICE?ECHOY_TONE:ECHOY_CHORD,
            .note=69,.notes={69},.note_count=1,.velocity=.7,.duration=.23,.max_duration=.23,
            .articulation=ECHOY_BROKEN,.layer=ECHOY_ANCHOR};
        echoy_audio_loops_t *a=NULL;assert(echoy_audio_prepare(&p,false,&a)==ECHOY_AUDIO_OK);
        double before=frequency_energy(a->samples[2],1320)/frequency_energy(a->samples[2],440);
        assert(echoy_audio_voice_for_speaker(&p,a));
        double fundamental=frequency_energy(a->samples[2],440);
        double third=frequency_energy(a->samples[2],1320);
        assert(fundamental>1 && third/fundamental>before*4);
        assert(frequency_energy(a->samples[2],550)<fundamental*.01);
        echoy_audio_free_loops(a);
    }
}
/* At MIDI 126, an incorrectly retained fourth harmonic would fold to about
 * 641 Hz. An envelope can leak a little energy there, but no new low note may
 * appear. MIDI 127's fundamental itself exceeds Nyquist and must be silent. */
static void rejects_folded_harmonics(void) {
    for(int note=126;note<=127;note++){
        echoy_plan_t p;assert(echoy_compose_mask(0x1048,ECHOY_HARMONY,2,.5,&p));
        p.count=p.source_count=1;
        p.source[0]=(echoy_seed_note_t){.step=2,.kind=ECHOY_KICK,.velocity=.7};
        p.events[0]=(echoy_note_t){.step=2,.kind=ECHOY_CHORD,.note=note,.notes={note},
            .note_count=1,.velocity=.7,.duration=.23,.max_duration=.23,.articulation=ECHOY_BROKEN};
        echoy_audio_loops_t *a=NULL;assert(echoy_audio_prepare(&p,false,&a)==ECHOY_AUDIO_OK);
        assert(echoy_audio_voice_for_speaker(&p,a));
        if(note==127){for(unsigned i=0;i<N;i++)assert(a->samples[2][i]==0);}
        else {
            const double hz=440*exp2((note-69)/12.0);
            assert(frequency_energy(a->samples[2],48000-4*hz)<frequency_energy(a->samples[2],hz)*.01);
        }
        echoy_audio_free_loops(a);
    }
}
/* The first in-phase speaker candidate raised this phrase's driven crest from
 * 10.45 to 15.57 dB and lost average output under the fixed peak limiter. The
 * accepted fixed-phase candidate stays below 12 dB on that exact plan. This is
 * a waveform regression, not a claim about the physical cause of distortion. */
static void retained_phrase_does_not_stack_large_peaks(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);
    uint8_t bytes[ECHOY_LINK_PLAN_MAX_BYTES];size_t n=fread(bytes,1,sizeof bytes,file);
    assert(fgetc(file)==EOF && fclose(file)==0);
    echoy_plan_t p;assert(echoy_link_plan_decode(bytes,n,&p));assert(p.role==ECHOY_HARMONY);
    echoy_audio_loops_t *a=NULL;assert(echoy_audio_prepare(&p,false,&a)==ECHOY_AUDIO_OK);
    assert(echoy_audio_voice_for_speaker(&p,a));
    echoy_device_pcm_t state;echoy_device_pcm_reset(&state);
    double before_energy=0,after_energy=0,peak=0;
    for(unsigned i=0;i<3*N;i++){
        float calibrated=a->samples[2][i%N]*echoy_office_role_gains[ECHOY_HARMONY];
        float input=(float)(calibrated*.7611111111111111*.45);
        int16_t pcm;echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&state,&input,&pcm,1,&stats));assert(!stats.clipped);
        if(i<N)continue;
        double driven=state.samples[state.sample_at];
        peak=fmax(peak,fabs(driven));before_energy+=driven*driven;
        double output=pcm/32767.0;after_energy+=output*output;
        assert(fabs(output)<=.600001);
    }
    double crest=20*log10(peak/sqrt(before_energy/(2*N)));
    assert(crest<12);
    assert(sqrt(after_energy/(2*N))>.23);
    printf("retained Harmony driven crest %.3f dB, PCM RMS %.6f\n",crest,sqrt(after_energy/(2*N)));
    echoy_audio_free_loops(a);
}
int main(int argc,char **argv){assert(argc==2);isolated_profile();allocation_failure_is_atomic();bad_input_is_atomic();legal_pitch_edges();preserves_note_with_upper_harmonics();rejects_folded_harmonics();retained_phrase_does_not_stack_large_peaks(argv[1]);puts("PASS speaker profile isolation, atomic failure, allocation, legal pitches, harmonic/alias probes and retained-phrase crest");}
