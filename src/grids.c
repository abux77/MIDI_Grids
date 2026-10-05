// Port of v0.6, derived from Emilie Gillet's Grids. GPL-3.0-or-later.
#include "grids.h"
#include "grids_data.h"
static const uint8_t map[5][5]={{10,8,0,9,11},{15,7,13,12,6},{18,14,4,5,3},{23,16,21,1,2},{24,19,17,20,22}};
/* Python's signed >> rounds downward, unlike C signed division. */
static int mix(int a,int b,unsigned f) {int p=(b-a)*(int)f;return a+(p>=0?p/256:-((-p+255)/256));}
uint8_t grids_level(unsigned s,unsigned p,unsigned x,unsigned y) {
 unsigned i=x>>6,j=y>>6,o=p*32+s,fx=(x<<2)&255,fy=(y<<2)&255;
 return mix(mix(nodes[map[i][j]][o],nodes[map[i+1][j]][o],fx),mix(nodes[map[i][j+1]][o],nodes[map[i+1][j+1]][o],fx),fy);
}
void grids_reset(grids_t *g){g->step=0;for(unsigned p=0;p<3;p++)g->perturbation[p]=0;}
uint8_t grids_step(grids_t *g,uint8_t *a){
 if(!g->step)for(unsigned p=0;p<3;p++){g->rng=g->rng*1664525u+1013904223u;g->perturbation[p]=((g->rng>>24)*(g->chaos>>2))>>8;}
 uint8_t hits=0;*a=0;
 for(unsigned p=0;p<3;p++){unsigned l=grids_level(g->step,p,g->x,g->y)+g->perturbation[p];if(l>255)l=255;if(l>255u-g->density[p]){hits|=1u<<p;if(l>192)*a|=1u<<p;}}
 g->step=(g->step+1)&31;return hits;
}
