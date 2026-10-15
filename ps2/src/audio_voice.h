#ifndef SLENDYTUBBIES_AUDIO_VOICE_H
#define SLENDYTUBBIES_AUDIO_VOICE_H
#include <stdint.h>
enum { AUDIO_ULAW=0x80000000u,AUDIO_COUNT=0x7fffffffu };
static inline int ulaw_decode(uint8_t byte) {
    unsigned code=(unsigned)(uint8_t)~byte;
    int value=(((code&15)<<3)+132)<<((code>>4)&7);
    return code&128?132-value:value-132;
}
static inline int voice_ulaw_sample(const uint8_t *data,unsigned samples,int *cursor,int loop) {
    int position=*cursor;
    if(position<0||!samples)return 0;
    if((unsigned)position>=samples){if(!loop){*cursor=-1;return 0;}position=0;}
    int sample=ulaw_decode(data[position++]);
    *cursor=(!loop&&(unsigned)position==samples)?-1:position;
    return sample;
}
/* One-shot cursor also represents whether the voice is still playing. */
static inline int voice_start_once(int *cursor,unsigned samples) {
    if(!samples||(*cursor>=0&&(unsigned)*cursor<samples))return 0;
    *cursor=0;return 1;
}
static inline int voice_start_ready(int *cursor,unsigned samples,uint64_t now,uint64_t audible_until) {
    return now>=audible_until&&voice_start_once(cursor,samples);
}
static inline int voice_sample(const int16_t *pcm,unsigned samples,int *cursor,int loop) {
    int position=*cursor;
    if(position<0||!samples)return 0;
    if((unsigned)position>=samples){if(!loop){*cursor=-1;return 0;}position=0;}
    int sample=pcm[position++];
    *cursor=(!loop&&(unsigned)position==samples)?-1:position;
    return sample;
}
#endif
