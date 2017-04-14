//
// Created by wei on 17-4-2.
//

#ifndef VOXEL_HASHING_POSITION_CONVERTER_H
#define VOXEL_HASHING_POSITION_CONVERTER_H

#include "common.h"
#include "hash_param.h"
#include "sensor_param.h"

extern __constant__ HashParams kHashParams;
extern __constant__ SensorParams kSensorParams;

///////////////////////////////////////////////////
/// Transforms between world, voxel, and block coordinate systems
/// Semantic: A pos To B pos; A, B in {world, voxel, block}
/// float is only used to do interpolation
__device__
inline float3 WorldToVoxelf(const float3& world_pos) {
  return world_pos / kHashParams.voxel_size;
}
__device__
inline int3 WorldToVoxeli(const float3& world_pos) {
  const float3 p = world_pos / kHashParams.voxel_size;
  return make_int3(p + make_float3(sign(p)) * 0.5f);
}

__device__
inline int3 VoxelToBlock(int3 voxel_pos) {
  if (voxel_pos.x < 0) voxel_pos.x -= SDF_BLOCK_SIZE-1;
  if (voxel_pos.y < 0) voxel_pos.y -= SDF_BLOCK_SIZE-1;
  if (voxel_pos.z < 0) voxel_pos.z -= SDF_BLOCK_SIZE-1;

  return make_int3(
          voxel_pos.x / SDF_BLOCK_SIZE,
          voxel_pos.y / SDF_BLOCK_SIZE,
          voxel_pos.z / SDF_BLOCK_SIZE);
}

/// Corner voxel with smallest xyz
__device__
inline int3 BlockToVoxel(const int3& block_pos) {
  return block_pos * SDF_BLOCK_SIZE;
}

__device__
inline float3 VoxelToWorld(const int3& voxel_pos) {
  return make_float3(voxel_pos) * kHashParams.voxel_size;
}

__device__
inline float3 BlockToWorld(const int3& block_pos) {
  return VoxelToWorld(BlockToVoxel(block_pos));
}

__device__
inline int3 WorldToBlock(const float3& world_pos) {
  return VoxelToBlock(WorldToVoxeli(world_pos));
}

/////////////////////////////////////////////
/// Transforms between coordinates and indices
/// Idx means local idx inside a block \in [0, 511]
__device__
inline uint3 IdxToVoxelLocalPos(uint idx) {
  uint x = idx % SDF_BLOCK_SIZE;
  uint y = (idx % (SDF_BLOCK_SIZE * SDF_BLOCK_SIZE)) / SDF_BLOCK_SIZE;
  uint z = idx / (SDF_BLOCK_SIZE * SDF_BLOCK_SIZE);
  return make_uint3(x, y, z);
}

/// Computes the linearized index of a local virtual voxel pos; pos \in [0;7]^3
__device__
inline uint VoxelLocalPosToIdx(const int3& voxel_local_pos) {
  return voxel_local_pos.z * SDF_BLOCK_SIZE * SDF_BLOCK_SIZE +
         voxel_local_pos.y * SDF_BLOCK_SIZE +
         voxel_local_pos.x;
}

__device__
inline int VoxelPosToIdx(const int3& voxel_pos) {
  int3 voxel_local_pos = make_int3(
          voxel_pos.x % SDF_BLOCK_SIZE,
          voxel_pos.y % SDF_BLOCK_SIZE,
          voxel_pos.z % SDF_BLOCK_SIZE);

  if (voxel_local_pos.x < 0) voxel_local_pos.x += SDF_BLOCK_SIZE;
  if (voxel_local_pos.y < 0) voxel_local_pos.y += SDF_BLOCK_SIZE;
  if (voxel_local_pos.z < 0) voxel_local_pos.z += SDF_BLOCK_SIZE;

  return VoxelLocalPosToIdx(voxel_local_pos);
}

__device__
inline int WorldPosToIdx(const float3& world_pos) {
  int3 voxel_pos = WorldToVoxeli(world_pos);
  return VoxelPosToIdx(voxel_pos);
}

///////////////////////////////////////////////////////////////
/// Projections and reprojections
/// Between the Camera coordinate system and the image plane
/// Projection
__device__
static inline float2 CameraProjectToImagef(const float3& pos)	{
  return make_float2(
          pos.x*kSensorParams.fx/pos.z + kSensorParams.cx,
          pos.y*kSensorParams.fy/pos.z + kSensorParams.cy);
}

__device__
static inline int2 CameraProjectToImagei(const float3& pos)	{
  float2 pImage = CameraProjectToImagef(pos);
  return make_int2(pImage + make_float2(0.5f, 0.5f));
}

__device__
static inline uint2 CameraProjectToImageui(const float3& pos)	{
  int2 p = CameraProjectToImagei(pos);
  return make_uint2(p.x, p.y);
}

/// R^3 -> [0, 1]^3
/// maybe used for rendering
__device__
static inline float NormalizeDepth(float z)	{
  return (z - kSensorParams.min_depth_range)
         /(kSensorParams.max_depth_range - kSensorParams.min_depth_range);
}

///////////////////////////////////////////////////////////////
// Screen to Camera (depth in meters)
///////////////////////////////////////////////////////////////
/// R^2 -> R^3
__device__
static inline float3 ImageReprojectToCamera(uint ux, uint uy, float depth)	{
  const float x = ((float)ux-kSensorParams.cx) / kSensorParams.fx;
  const float y = ((float)uy-kSensorParams.cy) / kSensorParams.fy;
  return make_float3(depth*x, depth*y, depth);
}

///////////////////////////////////////////////////////////////
// RenderScreen to Camera -- ATTENTION ASSUMES [1,0]-Z range!!!!
///////////////////////////////////////////////////////////////
/// [0, 1]^3 -> R^3
__device__ /// Normalize
static inline float DenormalizeDepth(float z) {
  return z * (kSensorParams.max_depth_range - kSensorParams.min_depth_range)
         + kSensorParams.min_depth_range;
}

__device__
static inline bool IsInCameraFrustumApprox(const float4x4& viewMatrixInverse, const float3& pos) {
  float3 p_camera = viewMatrixInverse * pos;
  float2 uv = CameraProjectToImagef(p_camera);
  float3 normalized_p = make_float3(
          (2.0f*uv.x - (kSensorParams.width- 1.0f))/(kSensorParams.width- 1.0f),
          ((kSensorParams.height-1.0f) - 2.0f*uv.y)/(kSensorParams.height-1.0f),
          NormalizeDepth(p_camera.z));

  normalized_p *= 0.95;
  return !(normalized_p.x < -1.0f || normalized_p.x > 1.0f
           || normalized_p.y < -1.0f || normalized_p.y > 1.0f
           || normalized_p.z < 0.0f || normalized_p.z > 1.0f);
}

#endif //VOXEL_HASHING_POSITION_CONVERTER_H