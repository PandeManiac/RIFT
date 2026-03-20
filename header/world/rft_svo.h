#pragma once

#include "world/rft_chunk.h"
#include "world/rft_voxel_face.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct rft_svo_node
{
	uint32_t first_child;
	uint8_t	 child_mask;
	uint8_t	 is_leaf;
	uint8_t	 padding[2];
} rft_svo_node;

typedef struct rft_svo_chunk
{
	rft_svo_node* nodes;
	uint32_t	  node_count;
	uint32_t	  capacity;
	uint8_t		  leaf_size;
	uint8_t		  padding[3];
} rft_svo_chunk;

void rft_svo_chunk_init(rft_svo_chunk* chunk);
void rft_svo_chunk_destroy(rft_svo_chunk* chunk);

bool rft_svo_chunk_build_from_heightmap(rft_svo_chunk* chunk,
										const uint8_t  heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE],
										uint8_t		   leaf_size);

bool rft_svo_chunk_build_from_chunk(rft_svo_chunk* chunk, const rft_chunk* source_chunk, uint8_t leaf_size);
bool rft_svo_chunk_sample(const rft_svo_chunk* chunk, uint32_t x, uint32_t y, uint32_t z);

size_t rft_svo_chunk_mesh(const rft_svo_chunk* self,
						  const rft_svo_chunk* neighbor_px,
						  const rft_svo_chunk* neighbor_nx,
						  const rft_svo_chunk* neighbor_py,
						  const rft_svo_chunk* neighbor_ny,
						  const rft_svo_chunk* neighbor_pz,
						  const rft_svo_chunk* neighbor_nz,
						  int32_t			   chunk_x,
						  int32_t			   chunk_y,
						  int32_t			   chunk_z,
						  rft_voxel_face*	   out_faces);

void   rft_svo_chunk_rasterize(const rft_svo_chunk* chunk, rft_chunk* out_chunk);
size_t rft_svo_chunk_memory_usage(const rft_svo_chunk* chunk);
