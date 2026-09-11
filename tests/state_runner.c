// SPDX-License-Identifier: Apache-2.0
/* Host-only replay of tests/oracle.mjs trajectories. Readiness delay and ppm
 * are reference simulation inputs, never hardware success criteria. */
#include "echoy_core.h"
#include <assert.h>
#include <stdio.h>
static echoy_core_t c;
static const char *roles[]={"pulse","voice","dust","harmony"};
static const char *transports[]={"paused","waiting","playing"};
static const char *lifecycle[]={"off","booting","ready"};
static const char *stages[]={"none","syncing","waiting","failed"};
static echoy_session_t *session(int d) {for(int j=0;j<3;j++)if(c.sessions[j].id&&c.sessions[j].id==c.devices[d].session_id)return &c.sessions[j];return NULL;}
static void action(int id,echoy_action_t type,double value) {
    echoy_input_t e={.type=type,.device=id,.at_us=c.time_us,.value=value,
        .boot_id=c.devices[id].boot_id,.generation=c.devices[id].role_generation};
    if(type==ECHOY_SYNC_READY)e.generation=c.sync.generation;
    assert(echoy_dispatch(&c,&e)==ECHOY_OK);
}
static void contact(bool connected) {echoy_input_t e={.type=ECHOY_CONTACTS,.at_us=c.time_us,.edge_count=connected?1:0,.edges={{0,1,false}}};assert(echoy_dispatch(&c,&e)==ECHOY_OK);}
static void start(int64_t at) {
    const uint64_t ids[]={1,2,3};echoy_core_init(&c,ids);echoy_tick(&c,at);
    c.devices[0].clock_ppm=28;c.devices[1].clock_ppm=-41;c.devices[2].clock_ppm=63;
    action(0,ECHOY_BOOT,0);action(1,ECHOY_BOOT,0);
}
static void ready(int64_t at) {
    echoy_tick(&c,at+380000);action(0,ECHOY_BOOT_READY,0);
    echoy_tick(&c,at+700000);action(1,ECHOY_BOOT_READY,0);
    echoy_tick(&c,at+710000);echoy_tick(&c,at+720000);
}
static void joined(int64_t at) {
    start(at);ready(at);contact(true);echoy_tick(&c,c.time_us+460000);
    echoy_tick(&c,c.sync.started_us+1051000);action(0,ECHOY_SYNC_READY,0);
    echoy_tick(&c,c.sync.apply_at_us+1000);
}
static void mask(unsigned m) {putchar('"');for(int i=0;i<3;i++)if(m&(1u<<i))putchar('A'+i);putchar('"');}
static void pitches(const double *a,size_t n,bool present) {if(!present){fputs("null",stdout);return;}putchar('[');for(size_t i=0;i<n;i++)printf("%s%.17g",i?",":"",a[i]);putchar(']');}
static void motif(const echoy_motif_t *m) {
    static const char *kinds[]={"kick","rim","hat","tone","grain","chord"};
    static const char *chords[]={"Dm","F","C","Gm","Bb","Am"};
    static const char *arts[]={"","short","broken","answer"};
    putchar('[');for(size_t i=0;i<m->count;i++) {
        const echoy_event_t *e=&m->events[i];
        printf("%s[%.17g,\"%s\",%.17g,%.17g,%.17g,%s,%.17g,",i?",":"",e->step,kinds[e->kind],e->vel,e->note,e->dur,e->accent?"true":"false",e->expression);
        pitches(e->notes,e->notes_count,e->has_notes);putchar(',');pitches(e->harmony,e->harmony_count,e->has_harmony);
        if(e->chord<0)fputs(",null,null,null]",stdout);else printf(",\"%s\",\"%s\",%.17g]",chords[e->chord],arts[e->articulation],e->max_duration);
    }putchar(']');
}
static void checkpoint(const char *label) {
    printf("{\"label\":\"%s\",\"at\":%.9f,\"devices\":[",label,c.time_us/1000000.0);
    for(int i=0;i<3;i++) {
        const echoy_device_t *d=&c.devices[i];echoy_session_t *s=session(i);
        printf("%s{\"role\":\"%s\",\"present\":%s,\"lifecycle\":\"%s\",\"transport\":\"%s\",\"activity\":%.17g,\"committedActivity\":%.17g,\"phrase\":%d,\"volume\":%.17g,\"motifAvailable\":%s,\"members\":",i?",":"",roles[d->role],d->lifecycle!=ECHOY_OFF?"true":"false",lifecycle[d->lifecycle],transports[d->transport],d->activity,d->committed_activity,d->phrase,d->volume,d->motif_available?"true":"false");
        mask(s?s->members:0);fputs(",\"leader\":",stdout);mask(s?(1u<<s->leader):0);fputs(",\"discovered\":",stdout);mask(d->discovered);
        fputs(",\"pendingRole\":",stdout);if(d->pending_role>=0)printf("\"%s\"",roles[d->pending_role]);else fputs("null",stdout);
        printf(",\"pendingPhrase\":%d}",d->pending_phrase);
    }
    fputs("],\"edges\":[",stdout);bool comma=false;
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)if(c.committed_edges&(1u<<(a*3+b))) {printf("%s\"%c>%c\"",comma?",":"",'A'+a,'A'+b);comma=true;}
    printf("],\"sync\":\"%s\",\"plan\":[",stages[c.sync.stage]);
    comma=false;for(int id=0;id<3;id++)for(size_t i=0;i<c.plan.count;i++) {
        const echoy_plan_entry_t *p=&c.plan.devices[i];if(p->device!=id)continue;
        printf("%s{\"id\":\"%c\",\"role\":\"%s\",\"source\":",comma?",":"",'A'+id,roles[p->role]);comma=true;
        if(p->source<0)fputs("null",stdout);else printf("\"%c\"",'A'+p->source);
        printf(",\"transport\":\"%s\",\"motifAvailable\":%s,\"eventCount\":%zu,\"motifCount\":%zu,\"motif\":",transports[p->transport],p->motif_available?"true":"false",p->events.count,p->motif.count);motif(&p->motif);putchar('}');
    }puts("]}");
}
int main(void) {
    start(10000000);checkpoint("powered-on-with-independent-clocks");ready(10000000);checkpoint("discovered-but-not-joined");
    start(20000000);ready(20000000);checkpoint("ready-independent");contact(true);checkpoint("contact-seen");
    echoy_tick(&c,c.time_us+460000);checkpoint("synchronizing");
    echoy_tick(&c,c.sync.started_us+1051000);action(0,ECHOY_SYNC_READY,0);checkpoint("beat-agreed-waiting-for-bar");
    echoy_tick(&c,c.sync.apply_at_us+1000);checkpoint("joined-at-future-bar");
    joined(30000000);checkpoint("joined-paused");action(1,ECHOY_PLAY_TOGGLE,0);checkpoint("play-request-waits-for-beat");
    int64_t play=c.devices[1].play_at_us;echoy_tick(&c,play-1000);checkpoint("still-waiting-before-beat");
    echoy_tick(&c,play+1000);checkpoint("playing-on-beat");
    joined(40000000);checkpoint("connected-response-active");contact(false);checkpoint("released-edge-still-committed");
    echoy_tick(&c,session(0)->arrangement_at_us+1000);checkpoint("response-removed-on-bar-shared-clock-kept");
    joined(50000000);
    /* The frozen JS fixture sets these fields directly. Match its setup only;
     * actual user transport paths are exercised by state_unit. */
    c.devices[0].transport=c.devices[1].transport=ECHOY_PLAYING;echoy_tick(&c,c.time_us);
    checkpoint("playing-with-valid-source");action(0,ECHOY_VOLUME,0);checkpoint("volume-zero-keeps-motif");
    action(0,ECHOY_OUTPUT_FAULT,1);checkpoint("output-fault-mutes-events-keeps-motif");
    action(0,ECHOY_OUTPUT_FAULT,0);action(0,ECHOY_DATA_FAULT,1);
    echoy_tick(&c,echoy_next_boundary(session(0),c.time_us+10000,true)+1000);checkpoint("data-fault-removes-motif-on-boundary");
    start(60000000);ready(60000000);checkpoint("pulse-default-memory");
    action(0,ECHOY_ROLE_REQUEST,ECHOY_HARMONY);action(0,ECHOY_PHRASE,3);action(0,ECHOY_ROLE_READY,0);checkpoint("idle-role-commit-restores-harmony-memory");
    action(0,ECHOY_ACTIVITY,.75);action(0,ECHOY_FINISH_ACTIVITY,0);checkpoint("idle-activity-commit");
    return 0;
}
