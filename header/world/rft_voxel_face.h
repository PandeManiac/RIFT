#pragma once

#include <stdint.h>

typedef uint64_t rft_voxel_face;

rft_voxel_face rft_voxel_face_pack(uint32_t x,
								   uint32_t y,
								   uint32_t z,
								   uint32_t w,
								   uint32_t h,
								   uint8_t	normal,
								   uint16_t material);
