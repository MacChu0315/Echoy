// SPDX-License-Identifier: Apache-2.0
#include "echoy_clock.h"
#include <math.h>
#include <string.h>
void echoy_clock_init(echoy_clock_t *c) {if(c)memset(c,0,sizeof *c);}
bool echoy_clock_observe(echoy_clock_t *c,int64_t t1,int64_t t2,int64_t t3,int64_t t4) {
    if(!c)return false;
    if(t1<0||t2<0||t3<t2||t4<t1||t1<=c->last_t1||t4<=c->last_t4) {c->rejected++;return false;}
    double rtt=((double)t4-t1)-((double)t3-t2);
    if(rtt<0||rtt>ECHOY_CLOCK_MAX_RTT_US){c->rejected++;return false;}
    c->last_t1=t1;c->last_t4=t4;
    double at=t1+((double)t4-t1)/2,offset=(((double)t2-t1)+((double)t3-t4))/2;
    /* Keep one sample just before the 20 s window to establish its span. */
    size_t first=0;while(first+1<c->count&&at-c->samples[first+1].at_us>ECHOY_CLOCK_WINDOW_US)first++;
    if(first){memmove(c->samples,c->samples+first,(c->count-first)*sizeof c->samples[0]);c->count-=first;}
    if(c->count==ECHOY_CLOCK_SAMPLES){memmove(c->samples,c->samples+1,(--c->count)*sizeof c->samples[0]);}
    c->samples[c->count++]=(echoy_clock_sample_t){at,offset,rtt};
    double min_rtt=rtt;for(size_t i=0;i<c->count;i++)min_rtt=fmin(min_rtt,c->samples[i].rtt_us);
    double sx=0,sy=0,sxx=0,sxy=0;size_t n=0;c->anchor_us=at;
    for(size_t i=0;i<c->count;i++)if(c->samples[i].rtt_us<=min_rtt+1000) {
        double x=(c->samples[i].at_us-at)/1000000.0,y=c->samples[i].offset_us;
        sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;n++;
    }
    c->stable=false;if(n<8)return true;
    double denom=n*sxx-sx*sx;if(denom<=0)return true;
    double slope=(n*sxy-sx*sy)/denom;c->rate=slope/1000000.0;c->offset_us=(sy-slope*sx)/n;
    double residual=0;
    for(size_t i=0;i<c->count;i++)if(c->samples[i].rtt_us<=min_rtt+1000) {
        double predicted=c->offset_us+(c->samples[i].at_us-at)*c->rate;
        residual=fmax(residual,fabs(c->samples[i].offset_us-predicted));
    }
    c->uncertainty_us=min_rtt/2+residual;
    c->stable=at-c->samples[0].at_us>=ECHOY_CLOCK_WINDOW_US&&fabs(c->rate)<=0.0005&&
        c->uncertainty_us<=ECHOY_CLOCK_MAX_UNCERTAINTY_US;
    return true;
}
bool echoy_clock_remote_to_local(const echoy_clock_t *c,int64_t remote,int64_t *local) {
    if(!c||!local||!c->stable||remote<0)return false;
    long double value=((long double)remote-c->offset_us+c->rate*c->anchor_us)/(1+c->rate);
    if(value<INT64_MIN||value>INT64_MAX)return false;
    *local=(int64_t)llroundl(value);return true;
}
