#include <assert.h>
#include <stdio.h>
#include "transport.h"
int main(void){
 transport_t t={.division=1};
 assert(!transport_receive(&t,1,0x90,100));
 assert(transport_receive(&t,1,0xfa,100)==0xfa&&t.running);
 assert(!transport_receive(&t,2,0xf8,101));
 for(unsigned d=1;d<=4;d*=2){t.division=d;t.phase=t.count=0;unsigned steps=0;for(unsigned n=0;n<96*d;n++)steps+=transport_step_due(&t);assert(steps==32);assert(t.phase==0&&t.count==0);}
 assert(transport_receive(&t,1,0xfc,200)==0xfc&&!t.running);
 unsigned phase=t.phase=7;
 assert(transport_receive(&t,1,0xfb,300)==0xfb&&t.running&&t.phase==phase);
 assert(!transport_timeout(&t,299,20833)); /* loop timestamp precedes RX */
 assert(!transport_timeout(&t,2000299,20833));
 assert(transport_timeout(&t,2000300,20833)&&!t.source&&t.running);
 assert(t.next_tick==2021133);
 assert(transport_receive(&t,2,0xf8,2000400)==0xf8&&t.source==2);
 assert(transport_receive(&t,2,0xfa,2000500)==0xfa&&t.phase==0);
 assert(transport_receive(&t,2,0xfc,2000600)==0xfc);
 assert(transport_timeout(&t,4000600,20833)&&!t.running);
 puts("Transport tests passed");
}
