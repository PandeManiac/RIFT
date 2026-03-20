#pragma once

#include "world/rft_voxel_face.h"
#include <stddef.h>
#include <stdint.h>

#define RFT_CHUNK_SIZE 64

typedef struct rft_chunk
{
	uint64_t columns[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE];
} rft_chunk;

size_t rft_chunk_mesh(const rft_chunk* chunk,
					  const rft_chunk* neighbor_px,
					  const rft_chunk* neighbor_nx,
					  const rft_chunk* neighbor_py,
					  const rft_chunk* neighbor_ny,
					  const rft_chunk* neighbor_pz,
					  const rft_chunk* neighbor_nz,
					  int			   chunk_origin_x,
					  int			   chunk_origin_y,
					  int			   chunk_origin_z,
					  rft_voxel_face*  out_faces);
