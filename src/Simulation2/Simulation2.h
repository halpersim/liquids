#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

#include "src/Utility/DXSample.h"

#include "computation.h"
#include "frame_constants.h"
#include "rendering.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace frame_constants;

class Simulation2 : public DXSample
{
public:
  Simulation2(UINT width, UINT height, std::wstring name);

  virtual void OnInit();
  virtual void OnUpdate();
  virtual void OnRender();
  virtual void OnDestroy();

  virtual void OnKeyDown(UINT8 /*key*/);

private:

  static constexpr UINT ThreadCount = 2;

  ComPtr<IDXGISwapChain3> swap_chain;
  ComPtr<ID3D12Device> device;

  ComPtr<ID3D12CommandQueue> compute_queue;
  ComPtr<ID3D12CommandQueue> graphics_queue;

  rendering render_obj;
  computation compute_obj;

  std::atomic<UINT64> render_fence_value;
  std::atomic<UINT64> compute_fence_value;
  
  std::mutex mutex;
  
  ComPtr<ID3D12Fence> render_fence;
  ComPtr<ID3D12Fence> compute_fence;

  HANDLE render_fence_event;
  HANDLE compute_fence_event;
  
  HANDLE compute_thread_handle;
  std::unique_ptr<std::thread> compute_thread;

  std::atomic_bool shut_down;
  UINT64 frame_fence_values[FRAME_COUNT];

  void LoadPipeline();
  void LoadAssets();
  void MoveToNextFrame(UINT old_backbuffer_idx);
  void WaitForGpu();

  void spawn_compute_thread();
  void async_compute_loop();
};
