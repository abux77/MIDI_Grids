#include "transport.h"
uint8_t transport_receive(transport_t *t,unsigned s,uint8_t b,uint64_t now){
 if(b!=0xf8&&b!=0xfa&&b!=0xfb&&b!=0xfc)return 0;
 if(t->source&&t->source!=s)return 0;
 t->source=s;
 if(b==0xf8){t->last_clock=now;t->clock_seen=true;}
 else if(b==0xfa){t->running=true;t->phase=t->count=0;t->last_clock=now;t->clock_seen=true;}
 else if(b==0xfb){t->running=true;t->last_clock=now;t->clock_seen=true;}
 else {t->running=false;t->last_clock=now;t->clock_seen=true;}
 return b;
}
bool transport_timeout(transport_t *t,uint64_t now,unsigned period){
 if(!t->source||!t->clock_seen||now<t->last_clock||now-t->last_clock<CLOCK_TIMEOUT_US)return false;
 t->source=0;t->clock_seen=false;t->count=0;t->next_tick=now+period;return true;
}
bool transport_step_due(transport_t *t){
 unsigned d=t->source?t->division:1;
 bool due=t->count==0&&t->phase%3==0;
 if(++t->count>=d){t->count=0;t->phase=(t->phase+1)%24;}
 return due;
}
