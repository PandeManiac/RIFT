#pragma once

#include "utils/rft_assert.h"
#include "world/rft_svo.h"

typedef enum rft_svo_region_state
{
	RFT_SVO_REGION_EMPTY,
	RFT_SVO_REGION_SOLID,
	RFT_SVO_REGION_PARTIAL
} rft_svo_region_state;

#define RFT_SVO_MAX_FACES (RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * 3 / 2)

uint8_t	 rft_svo_sanitize_leaf_size(uint8_t leaf_size);
bool	 rft_svo_chunk_reserve(rft_svo_chunk* chunk, uint32_t additional);
void	 rft_svo_chunk_compact(rft_svo_chunk* chunk);
uint32_t rft_svo_child_rank(uint8_t child_mask, uint32_t child);
