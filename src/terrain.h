#ifndef MC_CE_TERRAIN_H
#define MC_CE_TERRAIN_H

#define WORLD_SIZE 256
#define WORLD_MAX_HEIGHT 128
#define SEA_LEVEL 63

enum BlockType {
    BLOCK_AIR=0,
    BLOCK_GRASS=1,
    BLOCK_DIRT=2,
    BLOCK_STONE=3,
    BLOCK_SAND=4,
    BLOCK_WATER=5
};

class Terrain {
public:
    Terrain();
    void Generate(unsigned long seed);
    int GetHeight(int x,int z) const;
    BlockType GetBlock(int x,int y,int z) const;

    /* Small editing hooks for the prototype input. */
    void RemoveBlockColumn(int x,int z);
    void AddBlockColumn(int x,int z);

private:
    unsigned char m_height[WORLD_SIZE][WORLD_SIZE];
};

#endif
