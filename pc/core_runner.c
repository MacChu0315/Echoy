// SPDX-License-Identifier: Apache-2.0
/* Fixed JSON-lines host adapter. Firmware core has no parser or I/O dependency. */
#include "echoy_music.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *start; size_t length, next; char type; } token_t;
typedef struct { const char *p; token_t tokens[2048]; size_t count; } parser_t;
static void whitespace(parser_t *p) { while(isspace((unsigned char)*p->p))p->p++; }
static bool number(const parser_t *p,int i,double *out);
static int parse(parser_t *p, int depth)
{
    whitespace(p); if(depth>8 || p->count>=2048)return -1;
    size_t id=p->count++; token_t *t=&p->tokens[id]; t->start=p->p;t->type=*p->p;
    if(*p->p=='[' || *p->p=='{'){
        char end=*p->p++=='['?']':'}'; whitespace(p);
        if(*p->p!=end)for(;;){
            if(end=='}'){whitespace(p); if(*p->p!='"'||parse(p,depth+1)<0)return -1;whitespace(p);if(*p->p!=':')return -1;p->p++;}
            if(parse(p,depth+1)<0)return -1;
            whitespace(p);if(*p->p==end)break;if(*p->p!=',')return -1;p->p++;
        }
        p->p++;
    }else if(*p->p=='"'){
        p->p++;while(*p->p && *p->p!='"'){if(*p->p=='\\' || (unsigned char)*p->p<32)return -1;p->p++;}
        if(*p->p!='"')return -1;
        p->p++;
    }else{
        const char *start=p->p;
        while(*p->p && !isspace((unsigned char)*p->p) && *p->p!=',' && *p->p!=']' && *p->p!='}')p->p++;
        if(start==p->p)return -1;
        t->length=(size_t)(p->p-start);
        bool literal=(t->length==4&&(!strncmp(start,"null",4)||!strncmp(start,"true",4)))||
            (t->length==5&&!strncmp(start,"false",5));
        double ignored;
        if(!literal&&!number(p,(int)id,&ignored))return -1;
    }
    t->length=(size_t)(p->p-t->start);t->next=p->count;return (int)id;
}
static bool equal(const token_t *t,const char *s) { return t->length==strlen(s) && !strncmp(t->start,s,t->length); }
static bool string_equal(const token_t *t,const char *s) { return t->type=='"' && t->length==strlen(s)+2 && !strncmp(t->start+1,s,t->length-2); }
static int field(const parser_t *p,int obj,const char *key)
{
    if(obj<0 || p->tokens[obj].type!='{')return -1;
    for(size_t i=(size_t)obj+1;i<p->tokens[obj].next;){
        size_t val=i+1;if(string_equal(&p->tokens[i],key))return (int)val;i=p->tokens[val].next;
    }return -1;
}
static bool number(const parser_t *p,int i,double *out)
{
    if(i<0)return false;
    const token_t *t=&p->tokens[i];if(t->type=='"' || t->type=='[' || t->type=='{')return false;
    const char *s=t->start,*limit=s+t->length;
    if(s<limit && *s=='-')s++;
    if(s==limit)return false;
    if(*s=='0')s++;
    else {if(*s<'1'||*s>'9')return false;while(s<limit && isdigit((unsigned char)*s))s++;}
    if(s<limit && *s=='.'){s++;const char *begin=s;while(s<limit && isdigit((unsigned char)*s))s++;if(s==begin)return false;}
    if(s<limit && (*s=='e'||*s=='E')){s++;if(s<limit && (*s=='+'||*s=='-'))s++;const char *begin=s;while(s<limit && isdigit((unsigned char)*s))s++;if(s==begin)return false;}
    if(s!=limit)return false;
    char *end;errno=0;double n=strtod(t->start,&end);
    if(errno || end!=t->start+t->length || !isfinite(n))return false;
    *out=n;return true;
}
static bool integer(const parser_t *p,int obj,const char *key,int *out)
{
    double n;if(!number(p,field(p,obj,key),&n)||n<0||n>2147483647.0||floor(n)!=n)return false;*out=(int)n;return true;
}
static const char *const kinds[]={"kick","rim","hat","tone","grain","chord"};
static const char *const roles[]={"pulse","voice","dust","harmony"};
static bool pitches(const parser_t *p,int obj,const char *key,double *notes,size_t *count,bool *present)
{
    int id=field(p,obj,key);*present=id>=0;*count=0;if(id<0)return true;
    if(p->tokens[id].type!='[')return false;
    for(size_t i=(size_t)id+1;i<p->tokens[id].next;i=p->tokens[i].next){
        if(*count==ECHOY_MAX_PITCHES||!number(p,(int)i,&notes[*count]))return false;
        (*count)++;
    }return true;
}
static bool input(parser_t *p,echoy_music_input_t *in,echoy_motif_t *left)
{
    memset(in,0,sizeof *in);memset(left,0,sizeof *left);
    int r=field(p,0,"role"),role=-1;for(int i=0;i<4;i++)if(r>=0&&string_equal(&p->tokens[r],roles[i]))role=i;
    if(role<0)return false;
    in->role=(echoy_role_t)role;
    if(!integer(p,0,"phrase",&in->phrase)||!integer(p,0,"bar",&in->bar)||!integer(p,0,"duplicate",&in->duplicate)||
       !number(p,field(p,0,"activity"),&in->activity)||!number(p,field(p,0,"bpm"),&in->bpm))return false;
    int m=field(p,0,"motif");if(m<0)return false;
    if(equal(&p->tokens[m],"null"))return true;
    if(p->tokens[m].type!='[')return false;
    in->left=left;
    for(size_t i=(size_t)m+1;i<p->tokens[m].next;i=p->tokens[i].next){
        if(left->count==ECHOY_MAX_EVENTS || p->tokens[i].type!='{')return false;
        echoy_event_t *e=&left->events[left->count++];e->chord=-1;
        int k=field(p,(int)i,"kind"),kind=-1;for(int j=0;j<6;j++)if(k>=0&&string_equal(&p->tokens[k],kinds[j]))kind=j;
        if(kind<0)return false;
        e->kind=(echoy_kind_t)kind;
        if(!number(p,field(p,(int)i,"step"),&e->step)||!number(p,field(p,(int)i,"vel"),&e->vel)||
           !number(p,field(p,(int)i,"dur"),&e->dur))return false;
        e->note=NAN;int note=field(p,(int)i,"note");if(note>=0&&!number(p,note,&e->note))return false;
        int accent=field(p,(int)i,"accent");
        if(accent>=0){if(!equal(&p->tokens[accent],"true")&&!equal(&p->tokens[accent],"false"))return false;e->accent=equal(&p->tokens[accent],"true");}
        if(!pitches(p,(int)i,"notes",e->notes,&e->notes_count,&e->has_notes)||!pitches(p,(int)i,"harmony",e->harmony,&e->harmony_count,&e->has_harmony))return false;
    }return true;
}
static void print_pitches(const double *notes,size_t count,bool present)
{
    if(!present){fputs("null",stdout);return;}
    putchar('[');for(size_t i=0;i<count;i++)printf("%s%.17g",i?",":"",notes[i]);putchar(']');
}
static void output(const echoy_motif_t *m)
{
    static const char *const chords[]={"Dm","F","C","Gm","Bb","Am"};
    static const char *const arts[]={"","short","broken","answer"};
    putchar('[');
    for(size_t i=0;i<m->count;i++){
        const echoy_event_t *e=&m->events[i];
        printf("%s[%.17g,\"%s\",%.17g,%.17g,%.17g,%s,%.17g,",i?",":"",e->step,kinds[e->kind],e->vel,e->note,e->dur,e->accent?"true":"false",e->expression);
        print_pitches(e->notes,e->notes_count,e->has_notes);putchar(',');print_pitches(e->harmony,e->harmony_count,e->has_harmony);
        if(e->chord<0)fputs(",null,null,null]",stdout);
        else printf(",\"%s\",\"%s\",%.17g]",chords[e->chord],arts[e->articulation],e->max_duration);
    }puts("]");
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--self-test")){puts("Echoy core runner ready");return 0;}
    if(argc!=1){fprintf(stderr,"usage: %s [--self-test]\n",argv[0]);return 2;}
    char line[16384];size_t lineno=0;
    while(fgets(line,sizeof line,stdin)){
        lineno++;parser_t p={.p=line};echoy_music_input_t in;echoy_motif_t left,out;
        bool parsed=strchr(line,'\n')!=NULL && parse(&p,0)==0;whitespace(&p);
        if(!parsed || *p.p || !input(&p,&in,&left) || !echoy_music_compose(&in,&out)){
            fprintf(stderr,"invalid input on line %zu\n",lineno);return 2;
        }output(&out);
    }return ferror(stdin)?2:0;
}
