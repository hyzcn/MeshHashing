//
// Created by wei on 17-4-2.
//
// Geometric utilities:
// 1. Transformation of Coordinate systems with different units
// 2. Projection, reprojection and viewing frustum determination
#ifndef VH_GEOMETRY_UTIL_H
#define VH_GEOMETRY_UTIL_H

#include "common.h"
#include "params.h"

extern __constant__ SDFParams kSDFParams;

//////////
/// Transforms between world, voxel, and block coordinate systems
/// Semantic: A pos To B pos; A, B in {world, voxel, block}
/// float is only used to do interpolation

/// World <-> Voxel
__device__
static inline
float3 WorldToVoxelf(const float3& world_pos) {
  return world_pos / kSDFParams.voxel_size;
}
__device__
static inline
int3 WorldToVoxeli(const float3& world_pos) {
  const float3 p = world_pos / kSDFParams.voxel_size;
  return make_int3(p + make_float3(sign(p)) * 0.5f);
}
__device__
static inline
float3 VoxelToWorld(const int3& voxel_pos) {
  return make_float3(voxel_pos) * kSDFParams.voxel_size;
}

/// Voxel <-> Block
__device__
static inline
int3 VoxelToBlock(int3 voxel_pos) {
  if (voxel_pos.x < 0) voxel_pos.x -= BLOCK_SIDE_LENGTH-1;
  if (voxel_pos.y < 0) voxel_pos.y -= BLOCK_SIDE_LENGTH-1;
  if (voxel_pos.z < 0) voxel_pos.z -= BLOCK_SIDE_LENGTH-1;

  return make_int3(voxel_pos.x / BLOCK_SIDE_LENGTH,
                   voxel_pos.y / BLOCK_SIDE_LENGTH,
                   voxel_pos.z / BLOCK_SIDE_LENGTH);
}
// Corner voxel with smallest xyz
__device__
static inline
int3 BlockToVoxel(const int3& block_pos) {
  return block_pos * BLOCK_SIDE_LENGTH;
}

/// Block <-> World
__device__
static inline
float3 BlockToWorld(const int3& block_pos) {
  return VoxelToWorld(BlockToVoxel(block_pos));
}
__device__
static inline
int3 WorldToBlock(const float3& world_pos) {
  return VoxelToBlock(WorldToVoxeli(world_pos));
}

//////////
/// Transforms between coordinates and indices
/// Idx means local idx inside a block \in [0, 512)
__device__
static inline
uint3 IdxToVoxelLocalPos(uint idx) {
  uint x = idx % BLOCK_SIDE_LENGTH;
  uint y = (idx % (BLOCK_SIDE_LENGTH * BLOCK_SIDE_LENGTH)) / BLOCK_SIDE_LENGTH;
  uint z = idx / (BLOCK_SIDE_LENGTH * BLOCK_SIDE_LENGTH);
  return make_uint3(x, y, z);
}

/// Computes the linearized index of a local virtual voxel pos; pos \in [0, 8)^3
__device__
static inline
uint VoxelLocalPosToIdx(const int3& voxel_local_pos) {
  return voxel_local_pos.z * BLOCK_SIDE_LENGTH * BLOCK_SIDE_LENGTH +
         voxel_local_pos.y * BLOCK_SIDE_LENGTH +
         voxel_local_pos.x;
}
__device__
static inline
int VoxelPosToIdx(const int3& voxel_pos) {
  int3 voxel_local_pos = make_int3(
          voxel_pos.x % BLOCK_SIDE_LENGTH,
          voxel_pos.y % BLOCK_SIDE_LENGTH,
          voxel_pos.z % BLOCK_SIDE_LENGTH);

  if (voxel_local_pos.x < 0) voxel_local_pos.x += BLOCK_SIDE_LENGTH;
  if (voxel_local_pos.y < 0) voxel_local_pos.y += BLOCK_SIDE_LENGTH;
  if (voxel_local_pos.z < 0) voxel_local_pos.z += BLOCK_SIDE_LENGTH;

  return VoxelLocalPosToIdx(voxel_local_pos);
}
__device__
static inline
int WorldPosToIdx(const float3& world_pos) {
  int3 voxel_pos = WorldToVoxeli(world_pos);
  return VoxelPosToIdx(voxel_pos);
}

//////////
/// Truncating distance
__device__
static inline
float truncate_distance(float z) {
  return kSDFParams.truncation_distance
         + kSDFParams.truncation_distance_scale * z;
}

//////////
/// Projections and reprojections
/// Between the Camera coordinate system and the image plane
/// Projection
__device__
static inline
float2 CameraProjectToImagef(const float3& camera_pos,
                             float fx, float fy, float cx, float cy)	{
  return make_float2(camera_pos.x * fx / camera_pos.z + cx,
                     camera_pos.y * fy / camera_pos.z + cy);
}
__device__
static inline
int2 CameraProjectToImagei(
        const float3& camera_pos,
        float fx, float fy, float cx, float cy)	{
  float2 uv = CameraProjectToImagef(camera_pos, fx, fy, cx, cy);
  return make_int2(uv + make_float2(0.5f, 0.5f));
}
__device__
static inline
float3 ImageReprojectToCamera(
        uint ux, uint uy, float depth,
        float fx, float fy, float cx, float cy)	{
  const float x = ((float)ux - cx) / fx;
  const float y = ((float)uy - cy) / fy;
  return make_float3(depth * x, depth * y, depth);
}

/// R^3 -> [0, 1]^3
/// maybe used for rendering
__device__
static inline
float NormalizeDepth(float z, float min_depth, float max_depth)	{
  return (z - min_depth) / (max_depth - min_depth);
}
static inline
float DenormalizeDepth(float z, float min_depth, float max_depth) {
  return z * (max_depth - min_depth) + min_depth;
}

/// View frustum test
__device__
static inline
bool IsPointInCameraFrustum(const float4x4& c_T_w, const float3& world_pos,
                            const SensorParams& sensor_params) {
  float3 camera_pos = c_T_w * world_pos;
  float2 uv = CameraProjectToImagef(camera_pos,
                                    sensor_params.fx, sensor_params.fy,
                                    sensor_params.cx, sensor_params.cy);
  float3 normalized_p = make_float3(
          (2.0f*uv.x - (sensor_params.width-1.0f))/(sensor_params.width-1.0f),
          ((sensor_params.height-1.0f)-2.0f*uv.y)/(sensor_params.height-1.0f),
          NormalizeDepth(camera_pos.z,
                         sensor_params.min_depth_range,
                         sensor_params.max_depth_range));

  normalized_p *= 0.95;
  return !(normalized_p.x < -1.0f || normalized_p.x > 1.0f
           || normalized_p.y < -1.0f || normalized_p.y > 1.0f
           || normalized_p.z < 0.0f || normalized_p.z > 1.0f);
}

__device__
static inline
bool IsBlockInCameraFrustum(float4x4 c_T_w, const int3& block_pos,
                            const SensorParams &sensor_params) {
  float3 world_pos = VoxelToWorld(BlockToVoxel(block_pos))
                     + kSDFParams.voxel_size * 0.5f * (BLOCK_SIDE_LENGTH - 1.0f);
  return IsPointInCameraFrustum(c_T_w, world_pos, sensor_params);
}

#endif //VH_GEOMETRY_UTIL_H
