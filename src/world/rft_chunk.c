#include "world/rft_chunk.h"
#include "utils/rft_hints.h"
#include "world/rft_terrain.h"

#include <immintrin.h>
#include <string.h>

static ALWAYS_INLINE inline __m256i rft_loadu256(const void* src)
{
	return _mm256_loadu_si256((const __m256i_u*)src);
}

static ALWAYS_INLINE inline void rft_storeu256(void* dst, __m256i value)
{
	_mm256_storeu_si256((__m256i_u*)dst, value);
}

static inline ALWAYS_INLINE void mesh_2d_plane(uint64_t*	   masks,
											   uint8_t		   normal,
											   uint32_t		   c_val,
											   uint8_t		   u_axis,
											   uint8_t		   v_axis,
											   uint8_t		   w_axis,
											   int			   chunk_origin_x,
											   int			   chunk_origin_y,
											   int			   chunk_origin_z,
											   rft_voxel_face* out,
											   size_t*		   count)
{
	for (uint32_t v = 0; v < 64; ++v)
	{
		while (masks[v])
		{
			uint64_t row		 = masks[v];
			uint32_t u			 = (uint32_t)__builtin_ctzll(row);
			uint64_t mask_from_u = row >> u;
			uint32_t w			 = (mask_from_u == ~0ULL) ? (64 - u) : (uint32_t)__builtin_ctzll(~mask_from_u);
			uint64_t u_mask		 = (w == 64) ? ~0ULL : (((1ULL << w) - 1ULL) << u);
			uint32_t h			 = 1;

			while (v + h < 64 && (masks[v + h] & u_mask) == u_mask)
			{
				masks[v + h] &= ~u_mask;
				h++;
			}

			uint32_t coords[3];
			coords[u_axis] = u;
			coords[v_axis] = v;
			coords[w_axis] = c_val;

			uint32_t final_w = w;
			uint32_t final_h = h;

			if (normal >= 2)
			{
				final_w = h;
				final_h = w;
			}

			uint8_t material = rft_terrain_material(chunk_origin_x + (int)coords[0], chunk_origin_y + (int)coords[1], chunk_origin_z + (int)coords[2], normal);
			out[(*count)++]	 = rft_voxel_face_pack(coords[0], coords[1], coords[2], final_w, final_h, normal, material);

			masks[v] &= ~u_mask;
		}
	}
}

static inline ALWAYS_INLINE void transpose64(uint64_t* m)
{
	uint64_t t;
	uint64_t mask = 0x00000000FFFFFFFFULL;

	for (int j = 32; j != 0; j >>= 1, mask ^= (mask << j))
	{
		for (int k = 0; k < 64; k = (k + j + 1) & ~j)
		{
			t = (m[k] >> j ^ m[k + j]) & mask;
			m[k] ^= (t << j);
			m[k + j] ^= t;
		}
	}
}

HOT size_t rft_chunk_mesh(const rft_chunk* chunk,
						  const rft_chunk* neighbor_px,
						  const rft_chunk* neighbor_nx,
						  const rft_chunk* neighbor_py,
						  const rft_chunk* neighbor_ny,
						  const rft_chunk* neighbor_pz,
						  const rft_chunk* neighbor_nz,
						  int			   chunk_origin_x,
						  int			   chunk_origin_y,
						  int			   chunk_origin_z,
						  rft_voxel_face*  out_faces)
{
	size_t count = 0;

	uint64_t masks_p[64];
	uint64_t masks_m[64];

	for (int x = 0; x < 64; ++x)
	{
		for (int z = 0; z < 64; z += 4)
		{
			__m256i col = rft_loadu256(&chunk->columns[x][z]);
			__m256i next;

			if (x < 63)
			{
				next = rft_loadu256(&chunk->columns[x + 1][z]);
			}

			else
			{
				next = neighbor_px ? rft_loadu256(&neighbor_px->columns[0][z]) : _mm256_setzero_si256();
			}

			rft_storeu256(&masks_p[z], _mm256_andnot_si256(next, col));
			__m256i prev;

			if (x > 0)
			{
				prev = rft_loadu256(&chunk->columns[x - 1][z]);
			}

			else
			{
				prev = neighbor_nx ? rft_loadu256(&neighbor_nx->columns[63][z]) : _mm256_setzero_si256();
			}
			rft_storeu256(&masks_m[z], _mm256_andnot_si256(prev, col));
		}

		mesh_2d_plane(masks_p, 0, (uint32_t)x, 1, 2, 0, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
		mesh_2d_plane(masks_m, 1, (uint32_t)x, 1, 2, 0, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
	}

	for (int z = 0; z < 64; ++z)
	{
		for (int x = 0; x < 64; x += 4)
		{
			uint64_t c0 = chunk->columns[x][z];
			uint64_t c1 = chunk->columns[x + 1][z];
			uint64_t c2 = chunk->columns[x + 2][z];
			uint64_t c3 = chunk->columns[x + 3][z];

			uint64_t n0;
			uint64_t n1;
			uint64_t n2;
			uint64_t n3;

			if (z < 63)
			{
				n0 = chunk->columns[x][z + 1];
				n1 = chunk->columns[x + 1][z + 1];
				n2 = chunk->columns[x + 2][z + 1];
				n3 = chunk->columns[x + 3][z + 1];
			}

			else if (neighbor_pz)
			{
				n0 = neighbor_pz->columns[x][0];
				n1 = neighbor_pz->columns[x + 1][0];
				n2 = neighbor_pz->columns[x + 2][0];
				n3 = neighbor_pz->columns[x + 3][0];
			}

			else
			{
				n0 = n1 = n2 = n3 = 0;
			}

			masks_p[x]	   = c0 & ~n0;
			masks_p[x + 1] = c1 & ~n1;
			masks_p[x + 2] = c2 & ~n2;
			masks_p[x + 3] = c3 & ~n3;

			if (z > 0)
			{
				n0 = chunk->columns[x][z - 1];
				n1 = chunk->columns[x + 1][z - 1];
				n2 = chunk->columns[x + 2][z - 1];
				n3 = chunk->columns[x + 3][z - 1];
			}

			else if (neighbor_nz)
			{
				n0 = neighbor_nz->columns[x][63];
				n1 = neighbor_nz->columns[x + 1][63];
				n2 = neighbor_nz->columns[x + 2][63];
				n3 = neighbor_nz->columns[x + 3][63];
			}

			else
			{
				n0 = n1 = n2 = n3 = 0;
			}

			masks_m[x]	   = c0 & ~n0;
			masks_m[x + 1] = c1 & ~n1;
			masks_m[x + 2] = c2 & ~n2;
			masks_m[x + 3] = c3 & ~n3;
		}

		mesh_2d_plane(masks_p, 4, (uint32_t)z, 1, 0, 2, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
		mesh_2d_plane(masks_m, 5, (uint32_t)z, 1, 0, 2, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
	}

	uint64_t slices_p[64][64];
	uint64_t slices_m[64][64];

	memset(slices_p, 0, sizeof(slices_p));
	memset(slices_m, 0, sizeof(slices_m));

	for (int z = 0; z < 64; ++z)
	{
		uint64_t face_masks_p[64];
		uint64_t face_masks_m[64];

		for (int x = 0; x < 64; ++x)
		{
			uint64_t col = chunk->columns[x][z];

			uint64_t next_y = col >> 1;
			if (neighbor_py)
			{
				next_y |= (neighbor_py->columns[x][z] & 1ULL) << 63;
			}

			uint64_t prev_y = col << 1;

			if (neighbor_ny)
			{
				prev_y |= (neighbor_ny->columns[x][z] >> 63);
			}

			else if (chunk_origin_y <= 0)
			{
				prev_y |= 1ULL;
			}

			face_masks_p[x] = col & ~next_y;
			face_masks_m[x] = col & ~prev_y;
		}

		transpose64(face_masks_p);
		transpose64(face_masks_m);

		for (int y = 0; y < 64; ++y)
		{
			slices_p[y][z] = face_masks_p[y];
			slices_m[y][z] = face_masks_m[y];
		}
	}

	for (int y = 0; y < 64; ++y)
	{
		mesh_2d_plane(slices_p[y], 2, (uint32_t)y, 0, 2, 1, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
		mesh_2d_plane(slices_m[y], 3, (uint32_t)y, 0, 2, 1, chunk_origin_x, chunk_origin_y, chunk_origin_z, out_faces, &count);
	}

	return count;
}
