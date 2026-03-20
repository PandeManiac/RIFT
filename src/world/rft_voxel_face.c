#include "world/rft_voxel_face.h"

rft_voxel_face rft_voxel_face_pack(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h, uint8_t normal, uint16_t material)
{
	uint64_t lo = (x & 63ULL) | ((y & 63ULL) << 6) | ((z & 63ULL) << 12) | ((normal & 7ULL) << 18) | ((uint64_t)material << 21);
	uint64_t hi = ((w - 1) & 63ULL) | (((h - 1) & 63ULL) << 6);

	return lo | (hi << 32);
}
