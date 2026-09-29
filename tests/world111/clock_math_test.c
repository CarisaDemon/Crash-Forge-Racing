#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "platform/native_world_math.h"
int main(void) {
    int rates[]={15,30,60,120,144,165,200,240,360,1000};
    for (unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++) {
        int rem=0,ticks=0,last=0,hz=rates[r];
        for (int f=1;f<=hz*10;f++) {
            int now=(int)((int64_t)f*960/hz);
            ticks+=NW_ConsumeClock(&rem,now-last); last=now;
            assert(rem>=0 && rem<32);
        }
        assert(ticks==300 && rem==0);
        printf("clock %4d FPS -> %d world ticks / 10 seconds OK\n",hz,ticks);
    }
    assert(NW_Lerp(INT32_MIN,INT32_MAX,0)==INT32_MIN);
    assert(NW_Lerp(INT32_MIN,INT32_MAX,65536)==INT32_MAX);
    assert(NW_Angle(4090,6,32768)==0);
    assert(NW_Angle(6,4090,32768)==0);
    int rem=7; assert(NW_ConsumeClock(&rem,64)==2 && rem==7);
    int old=-1000;
    for(int a=0;a<=65536;a++) { int x=NW_Lerp(-1000,1000,a); assert(x>=old); old=x; }
    puts("interpolation endpoints, negative positions, wrap and catch-up OK");
    return 0;
}
