// SPDX-License-Identifier: Apache-2.0
/* Offline artifacts, deliberately outside the allocation-free firmware DSP. */
#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void u16(FILE *f,unsigned n){fputc(n&255,f);fputc((n>>8)&255,f);}
static void u32(FILE *f,unsigned n){u16(f,n);u16(f,n>>16);}
static void wave_header(FILE *f,unsigned frames){fwrite("RIFF",1,4,f);u32(f,36+frames*4);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,2);u32(f,48000);u32(f,192000);u16(f,4);u16(f,16);fwrite("data",1,4,f);u32(f,frames*4);}
static const char *kinds[]={"kick","rim","hat","tone","grain","chord"};
static const char *arts[]={"none","short","broken","answer"};
static void json_note(FILE *f,const echoy_event_t *e,double when,bool comma){
    fprintf(f,"%s{\"when\":%.12g,\"kind\":\"%s\",\"vel\":%.12g,\"note\":%.12g,\"dur\":%.12g,\"expression\":%.12g,\"maxDuration\":%.12g,\"articulation\":\"%s\"",comma?",\n":"",when,kinds[e->kind],e->vel,isfinite(e->note)?e->note:62,e->dur,e->expression,e->max_duration?e->max_duration:2.45,arts[e->articulation]);
    if(e->has_notes){fputs(",\"notes\":[",f);for(size_t i=0;i<e->notes_count;i++)fprintf(f,"%s%.12g",i?",":"",e->notes[i]);fputc(']',f);}fputc('}',f);
}
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s output-directory\n",argv[0]);return 2;}
    static const double initial[]={.58,.56,.48,.50};
    static const char *roles[]={"pulse","voice","dust","harmony"};
    for(int index=0;index<22;index++){
        bool phrase=index<12;int kind=phrase?0:index-12;if(kind>5)kind=5;if(index==21)kind=0;
        char base[1024],path[1100];
        if(phrase)snprintf(base,sizeof base,"%s/%s-%d",argv[1],roles[index/3],index%3+1);
        else if(index==21)snprintf(base,sizeof base,"%s/single-kick-aligned",argv[1]);
        else if(index<18)snprintf(base,sizeof base,"%s/single-%s",argv[1],kinds[kind]);
        else snprintf(base,sizeof base,"%s/single-chord-%s",argv[1],arts[index-17]);
        snprintf(path,sizeof path,"%s.wav",base);FILE *wav=fopen(path,"wb");if(!wav){perror(path);return 1;}
        snprintf(path,sizeof path,"%s.json",base);FILE *json=fopen(path,"w");if(!json){perror(path);fclose(wav);return 1;}
        echoy_audio_t *a=malloc(echoy_audio_size());assert(a);assert(echoy_audio_init(a,echoy_audio_size(),0));
        unsigned frames=phrase?48000*9:48000*4;wave_header(wav,frames);fprintf(json,"{\"sampleRate\":48000,\"frames\":%u,\"events\":[\n",frames);
        bool comma=false;
        if(phrase){
            for(int bar=0;bar<2;bar++){
                echoy_motif_t m;echoy_music_input_t in={(echoy_role_t)(index/3),index%3+1,bar,0,initial[index/3],96,NULL};assert(echoy_music_compose(&in,&m));
                for(size_t i=0;i<m.count;i++){double when=.1+bar*2.5+m.events[i].step*.15625;assert(echoy_audio_schedule(a,0,0,llround(when*1e6),&m.events[i]));json_note(json,&m.events[i],when,comma);comma=true;}
            }
        }else{
            echoy_event_t e={.kind=(echoy_kind_t)kind,.note=62,.dur=.22,.vel=.8,.max_duration=2.45};
            if(kind==5){e.has_notes=true;e.notes_count=3;e.notes[0]=62;e.notes[1]=65;e.notes[2]=69;if(index>=18)e.articulation=(echoy_articulation_t)(index-17);}
            double when=index==21?.128:.1;assert(echoy_audio_schedule(a,0,0,llround(when*1e6),&e));json_note(json,&e,when,false);
        }
        fputs("\n]}\n",json);if(fclose(json)){perror("json close");return 1;}
        int16_t block[512];for(unsigned i=0;i<frames;){unsigned n=frames-i>256?256:frames-i;echoy_audio_pcm(a,block,n);for(unsigned j=0;j<n*2;j++)u16(wav,(uint16_t)block[j]);i+=n;}
        if(ferror(wav)||fclose(wav)){perror("WAV write");return 1;}
        echoy_audio_stats_t s=echoy_audio_stats(a);printf("%s started=%llu peak_voices=%u missed=%llu capacity=%llu clipped=%llu\n",base,(unsigned long long)s.started,s.peak_voices,(unsigned long long)s.missed,(unsigned long long)s.capacity_errors,(unsigned long long)s.clipped_samples);
        assert(!s.capacity_errors&&!s.missed&&!s.clipped_samples);free(a);
    }
    return 0;
}
