#include "terrain.h"

static unsigned long g_seed;

static unsigned long Hash2D(int x, int z, unsigned long seed)
{
    unsigned long h = seed;
    h ^= (unsigned long)(x * 374761393L);
    h ^= (unsigned long)(z * 668265263L);
    h ^= h >> 13;
    h *= 1274126177UL;
    h ^= h >> 16;
    return h;
}

static int ValueAt(int x, int z, unsigned long seed)
{
    return (int)(Hash2D(x, z, seed) & 255UL);
}

static int Smooth(int t)
{
    return (t * t * (767 - 2 * t)) >> 16;
}

static int Lerp8(int a, int b, int t)
{
    return a + (((b - a) * t) >> 8);
}

static int Noise2D(int x, int z, int cell, unsigned long seed)
{
    int gx = x / cell;
    int gz = z / cell;
    int tx = ((x % cell) * 255) / cell;
    int tz = ((z % cell) * 255) / cell;
    int sx = Smooth(tx);
    int sz = Smooth(tz);
    int a = ValueAt(gx, gz, seed);
    int b = ValueAt(gx + 1, gz, seed);
    int c = ValueAt(gx, gz + 1, seed);
    int d = ValueAt(gx + 1, gz + 1, seed);
    int ab = Lerp8(a, b, sx);
    int cd = Lerp8(c, d, sx);
    return Lerp8(ab, cd, sz);
}

Terrain::Terrain()
{
    int x;
    int z;
    for (z = 0; z < WORLD_SIZE; ++z)
        for (x = 0; x < WORLD_SIZE; ++x)
            m_height[z][x] = SEA_LEVEL;
}

void Terrain::Generate(unsigned long seed)
{
    int x;
    int z;

    g_seed = seed;

    for (z = 0; z < WORLD_SIZE; ++z) {
        for (x = 0; x < WORLD_SIZE; ++x) {
            int continent = Noise2D(x, z, 96, seed);
            int hills = Noise2D(x, z, 48, seed + 0x13579BDFUL);
            int detail = Noise2D(x, z, 16, seed + 0x2468ACE0UL);
            int mountains = Noise2D(x, z, 32, seed + 0x55AA1234UL);
            int h;

            h = 48 + ((continent - 128) * 42) / 255;
            h += ((hills - 128) * 22) / 255;
            h += ((detail - 128) * 7) / 255;

            if (mountains > 145 && continent > 125) {
                int m = mountains - 145;
                h += (m * m) / 700;
            }

            if (h < 48)
                h = 48 + (h - 48) / 2;

            if (h < 38)
                h = 38;
            if (h > WORLD_MAX_HEIGHT - 2)
                h = WORLD_MAX_HEIGHT - 2;

            m_height[z][x] = (unsigned char)h;
        }
    }

    for (z = 1; z < WORLD_SIZE - 1; ++z) {
        for (x = 1; x < WORLD_SIZE - 1; ++x) {
            int center = m_height[z][x];
            int avg = center;
            avg += m_height[z - 1][x];
            avg += m_height[z + 1][x];
            avg += m_height[z][x - 1];
            avg += m_height[z][x + 1];
            avg /= 5;
            m_height[z][x] = (unsigned char)((center * 3 + avg) / 4);
        }
    }
}

int Terrain::GetHeight(int x,int z) const
{
    if (x < 0 || x >= WORLD_SIZE || z < 0 || z >= WORLD_SIZE)
        return 0;
    return m_height[z][x];
}

BlockType Terrain::GetBlock(int x,int y,int z) const
{
    int h;

    if (x < 0 || x >= WORLD_SIZE ||
        z < 0 || z >= WORLD_SIZE ||
        y < 0 || y >= WORLD_MAX_HEIGHT)
        return BLOCK_AIR;

    h = GetHeight(x,z);

    if (y > h)
        return (y <= SEA_LEVEL) ? BLOCK_WATER : BLOCK_AIR;

    if (h <= SEA_LEVEL + 2 && y == h)
        return BLOCK_SAND;

    if (y == h)
        return BLOCK_GRASS;

    if (y >= h - 3)
        return BLOCK_DIRT;

    return BLOCK_STONE;
}

void Terrain::RemoveBlockColumn(int x,int z)
{
    if (x < 0 || x >= WORLD_SIZE || z < 0 || z >= WORLD_SIZE)
        return;
    if (m_height[z][x] > 2)
        --m_height[z][x];
}

void Terrain::AddBlockColumn(int x,int z)
{
    if (x < 0 || x >= WORLD_SIZE || z < 0 || z >= WORLD_SIZE)
        return;
    if (m_height[z][x] < WORLD_MAX_HEIGHT - 2)
        ++m_height[z][x];
}
