//
// Created by wei on 17-3-26.
//
#include <string>
#include <vector>
#include <cuda_runtime.h>
#include <glog/logging.h>
#include <opencv2/opencv.hpp>

#include <helper_cuda.h>
#include <chrono>

#include <string>
#include <cuda_runtime.h>
#include <glog/logging.h>
#include <opencv2/opencv.hpp>

#include "util/timer.h"
#include <queue>
#include <engine/visualizing_engine.h>
#include <io/mesh_writer.h>
#include <meshing/marching_cubes.h>
#include <visualization/compress_mesh.h>

#include "sensor/rgbd_local_sequence.h"
#include "sensor/rgbd_sensor.h"
#include "visualization/ray_caster.h"

#include "io/config_manager.h"
#include "core/collect_block_array.h"
#include "glwrapper.h"

#define DEBUG_

Light light = {
  {
      glm::vec3(0, -2, 0),
      glm::vec3(4, -2, 0)
  },
  glm::vec3(1, 1, 1),
  3.0f
};

int main(int argc, char** argv) {
  /// Use this to substitute tedious argv parsing
  RuntimeParams args;
  LoadRuntimeParams("../config/args.yml", args);

  ConfigManager config;
  RGBDLocalSequence rgbd_local_sequence;

  DatasetType dataset_type = DatasetType(args.dataset_type);
  config.LoadConfig(dataset_type);
  rgbd_local_sequence.LoadDataset(dataset_type);
  Sensor    sensor(config.sensor_params);

  MainEngine main_engine(config.hash_params,
                         config.mesh_params,
                         config.sdf_params);
  main_engine.ConfigVisualizingEngineMesh(light, args.free_walk, !args.mesh_range, args.bounding_box);
  if (args.ray_casting) {
    main_engine.ConfigVisualizingEngineRaycaster(config.ray_caster_params);
  }
  if (args.record_video) {
    main_engine.ConfigLoggingEngine(".", args.record_video, args.save_mesh);
  }
  main_engine.use_fine_gradient() = args.fine_gradient;

  cv::Mat color, depth;
  float4x4 wTc, cTw;
  int frame_count = 0;
  while (rgbd_local_sequence.ProvideData(depth, color, wTc)) {
    frame_count ++;
    if (args.run_frames > 0 &&  frame_count > args.run_frames)
      break;

    sensor.Process(depth, color);
    sensor.set_transform(wTc);
    cTw = wTc.getInverse();
//
//    float3 camera_pos = make_float3(wTc.m14, wTc.m24, wTc.m34);
//    float scale = 0.25;
//    float4 v04 = wTc * make_float4(scale, scale, 2*scale, 1);
//    float4 v14 = wTc * make_float4(scale, -scale, 2*scale, 1);
//    float4 v24 = wTc * make_float4(-scale, scale, 2*scale, 1);
//    float4 v34 = wTc * make_float4(-scale, -scale, 2*scale, 1);
//    float3 v0 = make_float3(v04.x, v04.y, v04.z);
//    float3 v1 = make_float3(v14.x, v14.y, v14.z);
//    float3 v2 = make_float3(v24.x, v24.y, v24.z);
//    float3 v3 = make_float3(v34.x, v34.y, v34.z);
//
//    std::vector<float3> vs = {camera_pos, v0, camera_pos, v1, camera_pos, v2, camera_pos, v3,
//                              v0, v1, v1, v3, v3, v2, v2, v0};
//
//    prev_cam_pos = camera_pos;

    main_engine.Mapping(sensor);
    main_engine.Meshing();
    main_engine.Recycle();
    main_engine.Visualize(cTw);

    main_engine.Log();
  }

  main_engine.FinalLog();

  return 0;
}