#include "terrain.h"
static unsigned long g_seed;
static unsigned long NextRandom(){ g_seed=g_seed*1664525UL+1013904223UL; return g_seed; }
static int SmoothValue(int a,int b,int t){ int s=(t*t*(768-2*t))>>16; return a+((b-a)*s>>8); }
static int NoiseGrid(int x,int z,int cell){
    int gx=x/cell,gz=z/cell,tx=((x%cell)*255)/cell,tz=((z%cell)*255)/cell;
    g_seed=(unsigned long)(gx*374761393L+gz*668265263L)^g_seed;
    int a=(int)(NextRandom()&255),b=(int)(NextRandom()&255),c=(int)(NextRandom()&255),d=(int)(NextRandom()&255);
    return SmoothValue(SmoothValue(a,b,tx),SmoothValue(c,d,tx),tz);
}
Terrain::Terrain(){ for(int z=0;z<WORLD_SIZE;++z)for(int x=0;x<WORLD_SIZE;++x)m_height[z][x]=1; }
void Terrain::Generate(unsigned long seed){
    g_seed=seed;
    for(int z=0;z<WORLD_SIZE;++z)for(int x=0;x<WORLD_SIZE;++x){
        int n1=NoiseGrid(x,z,64),n2=NoiseGrid(x,z,32),n3=NoiseGrid(x,z,16);
        int h=10+(n1*18)/255+(n2*8)/255+(n3*4)/255;
        if(h<2)h=2; if(h>WORLD_MAX_HEIGHT-1)h=WORLD_MAX_HEIGHT-1;
        m_height[z][x]=(unsigned char)h;
    }
}
int Terrain::GetHeight(int x,int z)const{if(x<0||x>=WORLD_SIZE||z<0||z>=WORLD_SIZE)return 0;return m_height[z][x];}
BlockType Terrain::GetBlock(int x,int y,int z)const{
    int h=GetHeight(x,z); if(x<0||x>=WORLD_SIZE||z<0||z>=WORLD_SIZE||y<0||y>=WORLD_MAX_HEIGHT||y>h)return BLOCK_AIR;
    if(y==h)return BLOCK_GRASS; if(y>=h-3)return BLOCK_DIRT; return BLOCK_STONE;
}
