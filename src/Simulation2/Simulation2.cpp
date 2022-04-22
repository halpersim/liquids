#include "src/Utility/stdafx.h"
#include "Simulation2.h" 

#include <chrono>
#include <functional>

//#define DEBUG_SYNCRO

Simulation2::Simulation2(UINT width, UINT height, std::wstring name) :
  DXSample(width, height, name),
  render_obj(width, height),
  render_fence_value(1),
  compute_fence_value(1),
  shut_down(false)
{
}

void Simulation2::OnInit(){
  LoadPipeline();
  LoadAssets();
  spawn_compute_thread();
}

void Simulation2::LoadPipeline(){
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the active device.
  {
    ComPtr<ID3D12Debug> debugController;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  if(m_useWarpDevice) {
    ComPtr<IDXGIAdapter> warpAdapter;
    ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    ThrowIfFailed(D3D12CreateDevice(
      warpAdapter.Get(),
      D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(&device)
    ));
  } else {
    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(
      hardwareAdapter.Get(),
      D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(&device)
    ));
  }

  D3D12_COMMAND_QUEUE_DESC graphics_queue_desc = {};
  graphics_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  graphics_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(device->CreateCommandQueue(&graphics_queue_desc, IID_PPV_ARGS(&graphics_queue)));
  graphics_queue->SetName(L"graphics_queue");


  D3D12_COMMAND_QUEUE_DESC compute_queue_desc = {};
  compute_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  compute_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

  ThrowIfFailed(device->CreateCommandQueue(&compute_queue_desc, IID_PPV_ARGS(&compute_queue)));
  compute_queue->SetName(L"compute_queue");

  
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FRAME_COUNT;
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(
    graphics_queue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
    Win32Application::GetHwnd(),
    &swapChainDesc,
    nullptr,
    nullptr,
    &swapChain
  ));

  // This sample does not support fullscreen transitions.
  ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

  ThrowIfFailed(swapChain.As(&swap_chain));

  render_obj.load_pipeline(device);
  compute_obj.load_pipeline(device);
}


void Simulation2::LoadAssets(){
  { //fences
    device->CreateFence(render_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&render_fence));
    device->CreateFence(compute_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute_fence));

    render_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    compute_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }

  {//vertex buffer 
    UINT num_elements = PARTICLE_COUNT;
    UINT stride = sizeof(float) * 3;

    ComPtr<ID3D12Resource> vertex_buffer_com;

    ThrowIfFailed(device->CreateCommittedResource(
      //&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      &CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(num_elements) * stride, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr,
      IID_PPV_ARGS(&vertex_buffer_com)
    ));

    std::vector<ComPtr<ID3D12Resource>> upload_heaps;  

    std::shared_ptr<buffer> vertex_buffer = std::make_shared<buffer>(vertex_buffer_com, num_elements, stride, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    vertex_buffer->get()->SetName(L"vertex_buffer");

   
    render_obj.load_assets(device, swap_chain, vertex_buffer);
    ID3D12CommandList* compute_list[] = {compute_obj.load_assets(device, vertex_buffer, upload_heaps) };

    compute_queue->ExecuteCommandLists(_countof(compute_list), compute_list);

    WaitForGpu();
  }
}


void Simulation2::spawn_compute_thread(){
  compute_thread = std::make_unique<std::thread>(std::bind(&Simulation2::async_compute_loop, this));
}

void Simulation2::async_compute_loop(){

  auto start = std::chrono::system_clock::now();

  while(!shut_down.load(std::memory_order_relaxed)) {
    auto now = std::chrono::system_clock::now();
    std::chrono::milliseconds duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

    ID3D12CommandList* command_lists[] = {compute_obj.populate_command_list(static_cast<float>(duration.count() * 0.001f))};

    {
      std::lock_guard<std::mutex> lock(mutex);

      UINT64 render_value = render_fence_value.load(std::memory_order_seq_cst);

      if(render_fence->GetCompletedValue() < render_value) {
        
#ifdef DEBUG_SYNCRO
        OutputDebugStringA((std::string("compute run #") + std::to_string(compute_fence_value + 1) + " waiting for render run #" + std::to_string(render_value) + "\n").c_str());
#endif
        compute_queue->Wait(render_fence.Get(), render_value);
      }

      compute_queue->ExecuteCommandLists(_countof(command_lists), command_lists);
      compute_queue->Signal(compute_fence.Get(), ++compute_fence_value);
    }

    compute_fence->SetEventOnCompletion(compute_fence_value.load(std::memory_order_relaxed), compute_fence_event);
    WaitForSingleObject(compute_fence_event, INFINITE);

   // shut_down.store(true);
  }
}

void Simulation2::OnRender(){
  UINT backbuffer_idx = swap_chain->GetCurrentBackBufferIndex();
  ID3D12CommandList* command_lists[] = {render_obj.populate_command_list(backbuffer_idx)};
  
  {
    std::lock_guard<std::mutex> lock(mutex);
    
    UINT64 compute_fence_val = compute_fence_value.load(std::memory_order_seq_cst);

    if(compute_fence->GetCompletedValue() < compute_fence_val) {
#ifdef DEBUG_SYNCRO
      OutputDebugStringA((std::string("render run #") + std::to_string(render_fence_value + 1) + " waiting for compute run #" + std::to_string(compute_fence_val)+ "\n").c_str());
#endif
      graphics_queue->Wait(compute_fence.Get(), compute_fence_val);
    }

    graphics_queue->ExecuteCommandLists(_countof(command_lists), command_lists);
        
    UINT64 fence_value = ++render_fence_value;
    frame_fence_values[backbuffer_idx] = fence_value;
    ThrowIfFailed(graphics_queue->Signal(render_fence.Get(), fence_value));
  }
  
  ThrowIfFailed(swap_chain->Present(1, 0));
    
  if(render_fence->GetCompletedValue() < frame_fence_values[swap_chain->GetCurrentBackBufferIndex()]) {
    render_fence->SetEventOnCompletion(frame_fence_values[swap_chain->GetCurrentBackBufferIndex()], render_fence_event);
    WaitForSingleObject(render_fence_event, INFINITE);
  }
}

void Simulation2::MoveToNextFrame(UINT old_backbuffer_idx){

}

void Simulation2::OnUpdate(){

}

void Simulation2::OnKeyDown(UINT8 key){

}

void Simulation2::OnDestroy(){
  shut_down.store(true, std::memory_order_seq_cst);
  compute_thread->join();
   
  graphics_queue->Signal(render_fence.Get(), render_fence_value);
  render_fence->SetEventOnCompletion(render_fence_value, render_fence_event);
  WaitForSingleObject(render_fence_event, INFINITE);

  CloseHandle(render_fence_event);
  CloseHandle(compute_fence_event);
}

void Simulation2::WaitForGpu(){
  render_fence_value.fetch_add(1, std::memory_order_relaxed);
  graphics_queue->Signal(render_fence.Get(), render_fence_value);
  render_fence->SetEventOnCompletion(render_fence_value, render_fence_event);
  WaitForSingleObject(render_fence_event, INFINITE);

  compute_fence_value.fetch_add(1, std::memory_order_relaxed);
  compute_queue->Signal(compute_fence.Get(), compute_fence_value);
  compute_fence->SetEventOnCompletion(compute_fence_value, compute_fence_event);
  WaitForSingleObject(compute_fence_event, INFINITE);
}