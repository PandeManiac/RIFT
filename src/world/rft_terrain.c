#include "world/rft_terrain.h"
#include "utils/rft_noise.h"

#include <math.h>

static float rft_terrain_saturate(float x)
{
	if (x < 0.0f)
	{
		return 0.0f;
	}

	if (x > 1.0f)
	{
		return 1.0f;
	}

	return x;
}

static float rft_terrain_lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

static float rft_terrain_smoothstep(float edge0, float edge1, float x)
{
	float t = (x - edge0) / (edge1 - edge0);
	t		= rft_terrain_saturate(t);
	return t * t * (3.0f - 2.0f * t);
}

uint8_t rft_terrain_height(int world_x, int world_z)
{
	float wx = (float)world_x;
	float wz = (float)world_z;

	float macro = rft_noise_fbm_2d(wx + 1400.0f, wz - 900.0f, 4, 0.5f, 0.0015f) * 0.5f + 0.5f;
	float biome = rft_noise_fbm_2d(wx - 2300.0f, wz + 1700.0f, 3, 0.56f, 0.0007f) * 0.5f + 0.5f;

	float plains_roll	= rft_noise_fbm_2d(wx + 320.0f, wz - 640.0f, 4, 0.55f, 0.0040f) * 0.5f + 0.5f;
	float plains_detail = rft_noise_fbm_2d(wx - 910.0f, wz + 430.0f, 3, 0.5f, 0.0150f) * 0.5f + 0.5f;

	float mountain_mass	  = rft_noise_fbm_2d(wx - 1200.0f, wz + 900.0f, 4, 0.56f, 0.0033f) * 0.5f + 0.5f;
	float mountain_ridges = 1.0f - fabsf(rft_noise_fbm_2d(wx + 780.0f, wz - 280.0f, 4, 0.6f, 0.0105f));
	float mountain_detail = rft_noise_fbm_2d(wx - 140.0f, wz + 1180.0f, 4, 0.52f, 0.0240f) * 0.5f + 0.5f;

	macro			= powf(rft_terrain_saturate(macro), 1.15f);
	plains_roll		= powf(rft_terrain_saturate(plains_roll), 1.15f);
	plains_detail	= rft_terrain_saturate(plains_detail);
	mountain_mass	= rft_terrain_saturate((mountain_mass - 0.25f) / 0.75f);
	mountain_mass	= powf(mountain_mass, 3.0f);
	mountain_ridges = powf(rft_terrain_saturate(mountain_ridges), 2.0f);
	mountain_detail = rft_terrain_saturate(mountain_detail);

	float biome_blend	= rft_terrain_smoothstep(0.17f, 0.46f, biome);
	float plains_height = 8.0f + macro * 7.0f + plains_roll * 5.0f + plains_detail * 2.0f;
	float hills_height	= 12.0f + macro * 11.0f + mountain_mass * 165.0f + mountain_ridges * 20.0f + mountain_detail * 9.0f;

	float height_f = rft_terrain_lerp(plains_height, hills_height, biome_blend);
	int	  height_i = (int)height_f;

	if (height_i < 1)
	{
		height_i = 1;
	}

	if (height_i > (int)RFT_TERRAIN_MAX_HEIGHT)
	{
		height_i = (int)RFT_TERRAIN_MAX_HEIGHT;
	}

	return (uint8_t)height_i;
}

uint8_t rft_terrain_material(int world_x, int world_y, int world_z, uint8_t face_normal)
{
	int surface = (int)rft_terrain_height(world_x, world_z) - 1;
	int depth	= surface - world_y;

	float dirt_noise = rft_noise_fbm_2d((float)world_x + 37.0f, (float)world_z - 51.0f, 3, 0.55f, 0.045f) * 0.5f + 0.5f;
	float rock_noise = rft_noise_fbm_2d((float)world_x - 211.0f, (float)world_z + 143.0f, 4, 0.6f, 0.03f) * 0.5f + 0.5f;

	if (face_normal == 2 && world_y >= surface)
	{
		return dirt_noise > 0.57f ? RFT_BLOCK_MATERIAL_COARSE_DIRT : RFT_BLOCK_MATERIAL_DIRT;
	}

	if (depth <= 2)
	{
		return dirt_noise > 0.67f ? RFT_BLOCK_MATERIAL_COARSE_DIRT : RFT_BLOCK_MATERIAL_DIRT;
	}

	if (depth > 18 || rock_noise > 0.62f)
	{
		return RFT_BLOCK_MATERIAL_BLACKSTONE;
	}

	return RFT_BLOCK_MATERIAL_STONE;
}
