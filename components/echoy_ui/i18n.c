// SPDX-License-Identifier: Apache-2.0
#include "echoy_view.h"
#include <stdio.h>
#include <string.h>
typedef struct {const char *key,*zh,*en;} text_t;
static const text_t texts[]={
{"pulse","脉冲","PULSE"},{"voice","声线","VOICE"},{"dust","微尘","DUST"},{"harmony","和声","HARMONY"},
{"pulse-title","脉冲","Pulse"},{"voice-title","声线","Voice"},{"dust-title","微尘","Dust"},{"harmony-title","和声","Harmony"},
{"pulse-detail","节奏","Rhythm"},{"voice-detail","旋律","Melody"},{"dust-detail","纹理","Texture"},{"harmony-detail","和弦","Chords"},
{"pulse-1","重音","Accents"},{"pulse-2","切分","Syncopation"},{"pulse-3","留白","Space"},
{"voice-1","短句","Short phrase"},{"voice-2","轻答","Light reply"},{"voice-3","长线","Long line"},
{"dust-1","颗粒","Grains"},{"dust-2","点缀","Touches"},{"dust-3","余韵","Trails"},
{"harmony-1","短和弦","Short chords"},{"harmony-2","分解","Arpeggio"},{"harmony-3","接句","Answer"},
{"role","角色","Role"},{"play","播放","Play"},{"pause","暂停","Pause"},{"rich","丰富","Rich"},{"simple","简洁","Simple"},
{"volume","音量","Volume"},{"low","请充电","Low"},{"choose","角色与乐句","Role & phrase"},{"choose-phrase","选择一种表达","Choose a phrase"},
{"preparing","准备声音","Preparing sounds"},{"attention","需要处理","Something needs attention"},
{"loading-data","读取声音素材","Loading sounds"},{"wait","请稍候。","Please wait."},
{"loading-copy","原来的演奏继续。\n准备好后，在下一小节切换。","Your current music continues.\nThe new sound enters at the next bar."},
{"cancel","取消","Cancel"},{"retry","重试","Retry"},{"keep","继续原来的演奏","Keep current sound"},
{"error-role","新角色没有准备好","New role not ready"},{"error-data","乐句暂不可用","Phrase unavailable"},{"error-audio","暂时无法发声","Audio unavailable"},
{"data-copy","重新准备后，在下一小节恢复。","Prepare again, then resume at the next bar."},{"audio-copy","请重试本机声音输出。","Retry this tile’s audio output."},
{"motif-recovery","声音已准备 · 下一小节恢复","Sound ready · Resumes next bar"},
{"play-preparing","准备播放 · 可点暂停取消","Preparing · Tap Pause to cancel"},
{"sync-failed","暂未对齐 · 继续原来的演奏","Not synced · Keeping current music"},
{"syncing","正在对拍 · 原演奏继续","Syncing · Current music continues"},
{"sync-waiting","已约定拍点 · 下一小节接入","Beat agreed · Joins next bar"},
{"waiting","等待下一拍 · 可点暂停取消","Next beat · Tap Pause to cancel"},
{"pending-activity","新节奏 · 下一小节生效","New rhythm · Next bar"},
{"moving","位置确认中 · 保持当前编排","Checking position · Keeping pattern"},
{"contact-confirming","接触待确认","Confirming contact"},
{"arrangement-pending","新关系 · 下一小节生效","New connection · Next bar"},
{"notice","已生效","Applied"},{"link-split","无线失联 · 本地独奏","Link lost · Playing solo"},
{"link-hold","同伴失联 · 暂时保持拍子","Peer lost · Holding the beat"},
{"normal-source","回应左邻","Replying to the left"},{"normal-leader","提出动机","Leading the phrase"},
{"normal-shared-solo","独奏 · 保留同场拍子","Solo · Keeping the shared beat"},{"normal-solo","独奏","Solo"},
{"paused-source","已暂停 · 回应关系保留","Paused · Response retained"},{"paused-shared","已暂停 · 同场拍子保留","Paused · Shared beat retained"},
{"paused-solo","已暂停 · 独奏就绪","Paused · Ready for solo"},
{"perc","打击","PERC."},{"run","演奏中","RUN"},{"ready","就绪","READY"},{"minor","D 小调","D / MINOR"},
{"response","回应","RESPONSE"},{"phrase","乐句","PHRASE"},{"staccato","短和弦","STACCATO"},{"arpeggio","分解","ARPEGGIO"},{"answer","接句","ANSWER"},
{"particles","颗粒 / 本机","PARTICLES / LOCAL"},{"air","空气感","AIR"},{"off","",""}
};
const char *echoy_ui_text(const char *key,bool en){for(size_t i=0;i<sizeof(texts)/sizeof(texts[0]);i++)if(!strcmp(key,texts[i].key))return en?texts[i].en:texts[i].zh;return "";}
static const char *roles[]={"pulse","voice","dust","harmony"};
void echoy_ui_feedback(const echoy_snapshot_t *s,int id,bool en,char *out,size_t n){
    const echoy_device_t *d=&s->devices[id];const char *key=echoy_feedback_key(s,id);char phrase[32];
    if(!strcmp(key,"pending-role")){int r=d->pending_role;snprintf(phrase,sizeof(phrase),"%s-%d",roles[r],d->pending_phrase?d->pending_phrase:d->phrase_memories[r]);snprintf(out,n,en?"%s · %s · Pending":"%s · %s 待生效",echoy_ui_text(roles[r],en),echoy_ui_text(phrase,en));}
    else if(!strcmp(key,"pending-phrase")){snprintf(phrase,sizeof(phrase),"%s-%d",roles[d->role],d->pending_phrase);snprintf(out,n,en?"%s · Next bar":"%s · 下一小节生效",echoy_ui_text(phrase,en));}
    else snprintf(out,n,"%s",echoy_ui_text(key,en));
}
