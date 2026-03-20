#pragma once

#include <stdint.h>

#define RFT_TERRAIN_MATERIAL_COUNT 4U
#define RFT_TERRAIN_MAX_HEIGHT 192U

typedef enum rft_block_material
{
	RFT_BLOCK_MATERIAL_BLACKSTONE  = 0,
	RFT_BLOCK_MATERIAL_COARSE_DIRT = 1,
	RFT_BLOCK_MATERIAL_DIRT		   = 2,
	RFT_BLOCK_MATERIAL_STONE	   = 3
} rft_block_material;

uint8_t rft_terrain_height(int world_x, int world_z);
uint8_t rft_terrain_material(int world_x, int world_y, int world_z, uint8_t face_normal);
