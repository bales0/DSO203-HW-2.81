#ifndef WILDCAT_CAL_TEXT_H
#define WILDCAT_CAL_TEXT_H
#include <stdint.h>
static inline void CalReference_Text(char out[20],unsigned range,int32_t uv)
{
    char digits[12];unsigned n=0,k=0,i,places=range<6?2:3;
    uint32_t value=uv<0?0:(uint32_t)uv;
    uint32_t rounded=range<6?(value+5)/10:(value+500)/1000;
    uint32_t divisor=range<6?100:1000,integer=rounded/divisor,fraction=rounded%divisor;
    do{digits[k++]=(char)('0'+integer%10);integer/=10;}while(integer);
    while(k)out[n++]=digits[--k];
    out[n++]='.';
    for(i=0;i<places;++i){divisor/=10;out[n++]=(char)('0'+fraction/divisor);fraction%=divisor;}
    out[n++]=' ';if(range<6)out[n++]='m';out[n++]='V';out[n]=0;
}
#endif
