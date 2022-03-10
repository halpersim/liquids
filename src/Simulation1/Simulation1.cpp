//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "src/Utility/stdafx.h"
#include "src/Simulation1/Simulation1.h"

#include <math.h>
#include <algorithm>
#include <limits.h>

const float Simulation1::VisiblityPassPointOffset = 0.1f;
const DXGI_FORMAT Simulation1::TextureFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

const XMVECTOR Simulation1::eye = XMVectorSet(-0.f, 1.75f, -5.5f, 1.f);

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
const float Simulation1::DeferredShadingTexturesClearValues[3][4] = {
  {NaN, NaN, NaN, NaN}, //pos
  {0.f, 0.f, 0.f, 1.f}, //normal
  {0.f, 0.f, 0.f, 0.f}  //color
};

const float Simulation1::TimeStep = 0.35f;

Simulation1::Simulation1(UINT width, UINT height, std::wstring name) :
  DXSample(width, height, name),
  m_frameIndex(0),
  m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
  m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
  m_fenceValues{},
  m_rtvDescriptorSize(0),
  m_cbvSrvUavDescriptorSize(0),
  m_computeSyncFenceValue(0),
  freeze(true)
{
}

void Simulation1::OnInit(){
  LoadPipeline();
  LoadAssets();
}

// Load the rendering pipeline dependencies.
void Simulation1::LoadPipeline(){
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

  if(m_useWarpDevice)
  {
    ComPtr<IDXGIAdapter> warpAdapter;
    ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    ThrowIfFailed(D3D12CreateDevice(
      warpAdapter.Get(),
      D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(&m_device)
    ));
  } else
  {
    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(
      hardwareAdapter.Get(),
      D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(&m_device)
    ));
  }

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

  D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
  computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

  ThrowIfFailed(m_device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&m_computeQueue)));

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = (std::max<int>)(2, FrameCount);
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(
    m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
    Win32Application::GetHwnd(),
    &swapChainDesc,
    nullptr,
    nullptr,
    &swapChain
  ));

  // This sample does not support fullscreen transitions.
  ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

  ThrowIfFailed(swapChain.As(&m_swapChain));
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FinalRenderTargetOffset + (std::max<int>)(2, FrameCount);
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC computeCbvSrvUavDesc = {};
    computeCbvSrvUavDesc.NumDescriptors = ComputeDescriptorsPerRun * ComputeRuns;
    computeCbvSrvUavDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    computeCbvSrvUavDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&computeCbvSrvUavDesc, IID_PPV_ARGS(&m_computeCbvSrvUavHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC graphicsCbvSrvUavDesc = {};
    graphicsCbvSrvUavDesc.NumDescriptors = GP_DescriptorOffset::ShadingTextures + _countof(m_deferredShadingTextures);
    graphicsCbvSrvUavDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    graphicsCbvSrvUavDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&graphicsCbvSrvUavDesc, IID_PPV_ARGS(&m_graphicsCbvSrvUavHeap)));

    m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  // Create frame resources.
  {       
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), FinalRenderTargetOffset, m_rtvDescriptorSize);

    // Create a RTV and a command allocator for each frame.
    for(UINT n = 0; n < (std::max<UINT>)(FrameCount, 2); n++)
    {
      ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
      m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
    
    for(UINT n = 0; n < FrameCount; n++) {
      ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
      ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeCommandAllocators[n])));
    }
  }
}

// Load the sample assets.
void Simulation1::LoadAssets(){
  //Create the root Signature
  {
    CD3DX12_DESCRIPTOR_RANGE1 shaderInputTableRange[1] = {};
    shaderInputTableRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    
    CD3DX12_DESCRIPTOR_RANGE1 textureTableRange[1] = {};
    textureTableRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
   
    CD3DX12_ROOT_PARAMETER1 rootParameter[2] = {};
    rootParameter[GP_Slots::ShaderInputTable].InitAsDescriptorTable(_countof(shaderInputTableRange), shaderInputTableRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParameter[GP_Slots::TextureTable].InitAsDescriptorTable(_countof(textureTableRange), textureTableRange, D3D12_SHADER_VISIBILITY_PIXEL);

    //the static sampler used to sample the textures containing the shading information in the deferred shading pass
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = 0.0f;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameter), rootParameter, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);


    CD3DX12_ROOT_PARAMETER1 computeRootParameters[2];
    computeRootParameters[CS_Slots::Table].InitAsDescriptorTable(_countof(ranges), ranges);
    computeRootParameters[CS_Slots::Constants].InitAsConstants(sizeof(ComputeShaderInput)/4, CS_Register::Constants);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
    computeRootSignatureDesc.Init_1_1(_countof(computeRootParameters), computeRootParameters);

    ComPtr<ID3DBlob> computeSignature;
    ComPtr<ID3DBlob> computeError;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &computeSignature, &computeError));
    ThrowIfFailed(m_device->CreateRootSignature(0, computeSignature->GetBufferPointer(), computeSignature->GetBufferSize(), IID_PPV_ARGS(&m_computeSignature)));
  }

  // Create the pipeline state, which includes compiling and loading shaders.
  {
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    {
      //------------Pipieline State for the Visibility Pass----------------------
      ComPtr<ID3DBlob> vertexShader;
      ComPtr<ID3DBlob> geometryShader;
      ComPtr<ID3DBlob> pixelShader;

      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/visibility/liquid.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/visibility/liquid.hlsl").c_str(), nullptr, nullptr, "GSMain", "gs_5_1", compileFlags, 0, &geometryShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/visibility/liquid.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

      D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
      };

      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
      psoDesc.pRootSignature = m_rootSignature.Get();
      psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
      psoDesc.GS = CD3DX12_SHADER_BYTECODE(geometryShader.Get());
      psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
      psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
      psoDesc.DepthStencilState.DepthEnable = TRUE;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      psoDesc.DepthStencilState.StencilEnable = FALSE;
      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.SampleMask = UINT_MAX;
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      psoDesc.NumRenderTargets = 0;
      psoDesc.SampleDesc.Count = 1;
      
      ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_GPSO.visibility.liquid)));

      vertexShader.Reset();

      //the visiblity pass is solely responsible for computing depth information,
      //therefore the ground can be rendered without a pixel shader
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/visibility/ground.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));

      psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
      psoDesc.GS = CD3DX12_SHADER_BYTECODE();
      psoDesc.PS = CD3DX12_SHADER_BYTECODE();
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      
      ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_GPSO.visibility.ground)));
    }

    {
      //------------Pipieline State for the Blending Pass----------------------
      ComPtr<ID3DBlob> vertexShader;
      ComPtr<ID3DBlob> geometryShader;
      ComPtr<ID3DBlob> pixelShader;

      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/blending/liquid.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/blending/liquid.hlsl").c_str(), nullptr, nullptr, "GSMain", "gs_5_1", compileFlags, 0, &geometryShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/blending/liquid.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

      D3D12_INPUT_ELEMENT_DESC inputElementDescsPoints[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      };
  
      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.InputLayout = {inputElementDescsPoints, _countof(inputElementDescsPoints)};
      psoDesc.pRootSignature = m_rootSignature.Get();
      psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
      psoDesc.GS = CD3DX12_SHADER_BYTECODE(geometryShader.Get());
      psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
      psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

      psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
      psoDesc.BlendState.IndependentBlendEnable = TRUE;

      constexpr UINT POS_IDX = DeferredShadingTexturesIndex::POS;
      psoDesc.BlendState.RenderTarget[POS_IDX].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      //if there is no blending for the position attribute,
      //each pixel will have a slightly different position each frame,
      //which results in visible lightning artifacts.
      //therefore the MAX operator is choosen for blending
      psoDesc.BlendState.RenderTarget[POS_IDX].BlendEnable = TRUE;
      psoDesc.BlendState.RenderTarget[POS_IDX].SrcBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[POS_IDX].DestBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[POS_IDX].BlendOp = D3D12_BLEND_OP_MAX;
      //alpha values don't really matter - set to default
      psoDesc.BlendState.RenderTarget[POS_IDX].SrcBlendAlpha = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[POS_IDX].DestBlendAlpha = D3D12_BLEND_ZERO;
      psoDesc.BlendState.RenderTarget[POS_IDX].BlendOpAlpha = D3D12_BLEND_OP_ADD;
      //logic blending is disabled              
      psoDesc.BlendState.RenderTarget[POS_IDX].LogicOpEnable = FALSE;

      constexpr UINT NORMAL_IDX = DeferredShadingTexturesIndex::NORMAL;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      //normals are blended by adding them together 
      //as described in the linked paper
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].BlendEnable = TRUE;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].SrcBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].DestBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].BlendOp = D3D12_BLEND_OP_ADD;
      //alpha doesn't really matter for normals either
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].SrcBlendAlpha = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].DestBlendAlpha = D3D12_BLEND_ZERO;
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].BlendOpAlpha = D3D12_BLEND_OP_ADD;
      //logic blending is disabled
      psoDesc.BlendState.RenderTarget[NORMAL_IDX].LogicOpEnable = FALSE;

      constexpr UINT COLOR_IDX = DeferredShadingTexturesIndex::COLOR;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; 
      //color values are by adding them together 
      //as described in the linked paper
      psoDesc.BlendState.RenderTarget[COLOR_IDX].BlendEnable = TRUE;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].SrcBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].DestBlend = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].BlendOp = D3D12_BLEND_OP_ADD;
      //the color vector holds the sum of all weights in its alpha channel
      //therefore alpha blending is explicitly enabled
      psoDesc.BlendState.RenderTarget[COLOR_IDX].SrcBlendAlpha = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].DestBlendAlpha = D3D12_BLEND_ONE;
      psoDesc.BlendState.RenderTarget[COLOR_IDX].BlendOpAlpha = D3D12_BLEND_OP_ADD;
      //logic blending is disabled
      psoDesc.BlendState.RenderTarget[COLOR_IDX].LogicOpEnable = FALSE;

      psoDesc.DepthStencilState.DepthEnable = TRUE;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      //the depth buffer has already been filled by the visibily pass
      //therfore it must be disabled, so that blending pass doesn't pollute it 
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
      psoDesc.DepthStencilState.StencilEnable = FALSE;

      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.SampleMask = UINT_MAX;
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      psoDesc.SampleDesc.Count = 1;

      psoDesc.NumRenderTargets = _countof(m_deferredShadingTextures);
      for(UINT i = 0; i<psoDesc.NumRenderTargets; i++) {
        psoDesc.RTVFormats[i] = TextureFormat;
      }
      
      ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_GPSO.blending.liquid)));

      vertexShader.Reset();
      pixelShader.Reset();

      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/blending/ground.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/blending/ground.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));
            
      D3D12_INPUT_ELEMENT_DESC inputElementDescsGround[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
          {"TC", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
          {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
      };

      psoDesc.InputLayout = {inputElementDescsGround, _countof(inputElementDescsGround)};
      psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
      psoDesc.GS = CD3DX12_SHADER_BYTECODE();
      psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

      ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_GPSO.blending.ground)));
    }

    {
      //------------Pipieline State for the Shading Pass----------------------
      ComPtr<ID3DBlob> vertexShader;
      ComPtr<ID3DBlob> pixelShader;

      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/shading/deferred_shading.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
      ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/graphics/shading/deferred_shading.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

      D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"TC", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      };

      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
      psoDesc.pRootSignature = m_rootSignature.Get();
      psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
      psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
      psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
      psoDesc.DepthStencilState.DepthEnable = FALSE;
      psoDesc.DepthStencilState.StencilEnable = FALSE;
      psoDesc.SampleMask = UINT_MAX;
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      psoDesc.NumRenderTargets = 1;
      psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      psoDesc.SampleDesc.Count = 1;

      ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_GPSO.deferred)));
    }


    {
      //------------Compute Shader pipeline State----------------------
      ComPtr<ID3DBlob> computeShader;
      ThrowIfFailed(D3DReadFileToBlob(GetAssetFullPath(L"./compute_shader.cso").c_str(), &computeShader));
      //ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"../../src/Simulation1/shader/compute/compute_shader.hlsl").c_str(), nullptr, nullptr, "CSMain", "cs_5_1", compileFlags, 0, &computeShader, nullptr));

      D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
      computePsoDesc.pRootSignature = m_computeSignature.Get();
      computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

      ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computePSO)));
    }
  }

  // Create the depth Buffer
  {
    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    optimizedClearValue.DepthStencil = {1.f, 0};

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
      D3D12_RESOURCE_STATE_DEPTH_WRITE,
      &optimizedClearValue,
      IID_PPV_ARGS(&m_depthBuffer)
    ));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Texture2D.MipSlice = 0;
    dsv.Flags = D3D12_DSV_FLAG_NONE;

    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
  }

  // Create the command list.
  ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_GPSO.visibility.liquid.Get(), IID_PPV_ARGS(&m_commandList)));
  ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocators[m_frameIndex].Get(), m_computePSO.Get(), IID_PPV_ARGS(&m_computeCommandList)));
  
  // Create two Buffers for the Lattice, so that the compute shader can read from one and write to the other
  // Create a Buffer to store the Points to be rendered
  // Create a Buffer which holds the arguments for an indirect draw call

  // The VertexCountPerInstance Attribute of the indirect draw call buffer will be the hidden counter for the PointBuffer
  // Everytime the compute shader appends a new point to the PointBuffer VertexCountPerInstance will be incremented,
  // therefore the right number of points will be rendered in the indirect draw call
  ComPtr<ID3D12Resource> latticeUploadBuffer;
  UINT* latticeBufferData;
  {
    UINT numLatticePoints = LatticeWith * LatticeHeight * LatticeDepth * LatticePointsPerUnit * LatticePointsPerUnit * LatticePointsPerUnit;
    UINT latticeBufferSize = numLatticePoints * BytePerLatticePoint;

    //ensure that the buffer can be addressed by uints in the shader
    if(latticeBufferSize % 4) {
      latticeBufferSize += 4 - (latticeBufferSize % 4);
    }

    for(UINT i = 0; i<LatticeBufferCount; i++) {
      ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(latticeBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_latticeBuffer[i])
      ));
    }

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(latticeBufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&latticeUploadBuffer)));

    latticeBufferData = generateInitialLatticeBufferData(latticeBufferSize);

    D3D12_SUBRESOURCE_DATA commandData = {};
    commandData.pData = latticeBufferData;
    commandData.RowPitch = latticeBufferSize;
    commandData.SlicePitch = commandData.RowPitch;

    UpdateSubresources(m_commandList.Get(), m_latticeBuffer[LatticeBufferIn].Get(), latticeUploadBuffer.Get(), 0, 0, 1, &commandData);

    CD3DX12_RESOURCE_BARRIER transitionBarriers[2] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_latticeBuffer[LatticeBufferIn].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      CD3DX12_RESOURCE_BARRIER::Transition(m_latticeBuffer[LatticeBufferOut].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };

    m_commandList->ResourceBarrier(_countof(transitionBarriers), transitionBarriers);

    UINT pointBufferSize = LatticeWith * LatticeHeight * LatticeDepth * LatticePointsPerUnit * LatticePointsPerUnit * LatticePointsPerUnit * sizeof(float) * 4 * PointsPerLattice;

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(pointBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr,
      IID_PPV_ARGS(&m_vertexInput.liquid.buffer)
    ));

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(sizeof(IndirectCommand), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&m_indirectDrawBuffer)
    ));

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(sizeof(IndirectCommand)),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&m_indirectDrawBufferReset)
    ));

    IndirectCommand* drawBufferReset = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_indirectDrawBufferReset->Map(0, &readRange, reinterpret_cast<void**>(&drawBufferReset)));

    drawBufferReset->drawArguments.InstanceCount = 1;
    drawBufferReset->drawArguments.VertexCountPerInstance = 0;
    drawBufferReset->drawArguments.StartInstanceLocation = 0;
    drawBufferReset->drawArguments.StartVertexLocation = 0;

    m_indirectDrawBufferReset->Unmap(0, nullptr);

    m_commandList->CopyBufferRegion(m_indirectDrawBuffer.Get(), 0, m_indirectDrawBufferReset.Get(), 0, sizeof(IndirectCommand));
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indirectDrawBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
  

    //create Descriptors
    D3D12_SHADER_RESOURCE_VIEW_DESC lattice_in_desc = {};
    lattice_in_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    lattice_in_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lattice_in_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lattice_in_desc.Buffer.FirstElement = 0;
    lattice_in_desc.Buffer.NumElements = numLatticePoints * BytePerLatticePoint / 4;
    lattice_in_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

    D3D12_UNORDERED_ACCESS_VIEW_DESC lattice_out_desc = {};
    lattice_out_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    lattice_out_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    lattice_out_desc.Buffer.FirstElement = 0;
    lattice_out_desc.Buffer.NumElements = numLatticePoints * BytePerLatticePoint / 4;
    lattice_out_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  
    D3D12_UNORDERED_ACCESS_VIEW_DESC point_buffer_desc = {};
    point_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
    point_buffer_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    point_buffer_desc.Buffer.FirstElement = 0;
    point_buffer_desc.Buffer.NumElements = pointBufferSize / (sizeof(float) * 4);
    point_buffer_desc.Buffer.StructureByteStride = sizeof(float) * 4;
    point_buffer_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    point_buffer_desc.Buffer.CounterOffsetInBytes = offsetof(IndirectCommand, drawArguments.VertexCountPerInstance);

    CD3DX12_CPU_DESCRIPTOR_HANDLE heap_handle(m_computeCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
    
    m_device->CreateShaderResourceView(m_latticeBuffer[LatticeBufferIn].Get(), &lattice_in_desc, heap_handle);
    heap_handle.Offset(m_cbvSrvUavDescriptorSize);
    m_device->CreateUnorderedAccessView(m_latticeBuffer[LatticeBufferOut].Get(), nullptr, &lattice_out_desc, heap_handle);
    heap_handle.Offset(m_cbvSrvUavDescriptorSize);
    m_device->CreateUnorderedAccessView(m_vertexInput.liquid.buffer.Get(), m_indirectDrawBuffer.Get(), &point_buffer_desc, heap_handle);
    heap_handle.Offset(m_cbvSrvUavDescriptorSize);

    m_device->CreateShaderResourceView(m_latticeBuffer[LatticeBufferOut].Get(), &lattice_in_desc, heap_handle);
    heap_handle.Offset(m_cbvSrvUavDescriptorSize);
    m_device->CreateUnorderedAccessView(m_latticeBuffer[LatticeBufferIn].Get(), nullptr, &lattice_out_desc, heap_handle);
    heap_handle.Offset(m_cbvSrvUavDescriptorSize);
    m_device->CreateUnorderedAccessView(m_vertexInput.liquid.buffer.Get(), m_indirectDrawBuffer.Get(), &point_buffer_desc, heap_handle);


    m_vertexInput.liquid.bufferView.BufferLocation = m_vertexInput.liquid.buffer->GetGPUVirtualAddress();
    m_vertexInput.liquid.bufferView.SizeInBytes = pointBufferSize;
    m_vertexInput.liquid.bufferView.StrideInBytes = sizeof(float) * 4;
  }

  //create the command signature for the execute indirect call
  {
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &argDesc;
    commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

    ThrowIfFailed(m_device->CreateCommandSignature(&commandSignatureDesc, nullptr, IID_PPV_ARGS(&m_commandSignature)));
  }

  //create and initialize the shader input buffers for rendering
  {
    ComPtr<ID3D12Resource> *resources[] = {
      &m_constShaderInput.matrix, &m_constShaderInput.visiblity, &m_constShaderInput.blending, &m_constShaderInput.shading
    };

    UINT buffer_size[] = {
      sizeof(MatrixShaderInput), sizeof(VisibilityShaderInput), sizeof(BlendingShaderInput), sizeof(DeferredShadingInput)
    };
    
    for(UINT i = 0; i<_countof(resources); i++) {
      buffer_size[i] = ((buffer_size[i] >> 8) + 1) << 8; //size must be a multiple of 256

      ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(buffer_size[i]),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&(*resources[i]))
      ));
    }

    MatrixShaderInput* matrices;
    VisibilityShaderInput* visibilityShaderInput;
    BlendingShaderInput* blendingShaderInput;
    DeferredShadingInput* deferredShadingInput;

    D3D12_RANGE read_range;
    read_range.Begin = 0;
    read_range.End = 0;

    //all matrices will be static during the simulation 
    //therefore they are calculated in advance 

    ThrowIfFailed(m_constShaderInput.matrix->Map(0, &read_range, reinterpret_cast<void**>(&matrices)));
    ThrowIfFailed(m_constShaderInput.visiblity->Map(0, &read_range, reinterpret_cast<void**>(&visibilityShaderInput)));
    ThrowIfFailed(m_constShaderInput.blending->Map(0, &read_range, reinterpret_cast<void**>(&blendingShaderInput)));
    ThrowIfFailed(m_constShaderInput.shading->Map(0, &read_range, reinterpret_cast<void**>(&deferredShadingInput)));

    const float radius = 1.f / (LatticePointsPerUnit);
    
    XMMATRIX viewMatrix = XMMatrixLookAtLH(eye, XMVectorSet(0.f, 0.f, 1.f, 1.f), XMVectorSet(0.f, 1.f, 0.f, 0.f));
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH(1.5708f, m_aspectRatio, 0.1f, 100.f);

    matrices->world_liquid = XMMatrixTranslation(-1.5f, 0.2f, -3.f);
    matrices->world_ground = XMMatrixTranslation(0.f, 0.f, 0.f);
    matrices->vp = XMMatrixMultiply(viewMatrix, projectionMatrix);

    visibilityShaderInput->eye = XMFLOAT3(eye.m128_f32);
    visibilityShaderInput->radius = radius;
    visibilityShaderInput->offset = VisiblityPassPointOffset;

    blendingShaderInput->invers_vw = XMMatrixInverse(nullptr, viewMatrix);
    blendingShaderInput->epsilon = VisiblityPassPointOffset;
    blendingShaderInput->radius = radius;

    deferredShadingInput->eye = XMFLOAT4(eye.m128_f32);
    deferredShadingInput->light = XMFLOAT4(0.f, 2.f, -3.f, 1.f);

    m_constShaderInput.matrix->Unmap(0, nullptr);
    m_constShaderInput.visiblity->Unmap(0, nullptr);
    m_constShaderInput.blending->Unmap(0, nullptr);
    m_constShaderInput.shading->Unmap(0, nullptr);
        
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_graphicsCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), GP_DescriptorOffset::Matrices, m_cbvSrvUavDescriptorSize);

    for(UINT i = 0; i<_countof(resources); i++) {
      D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;

      cbvDesc.BufferLocation = (*resources[i])->GetGPUVirtualAddress();
      cbvDesc.SizeInBytes = buffer_size[i];
      m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);

      cbvHandle.Offset(1, m_cbvSrvUavDescriptorSize);
    }
  }

  //Create the textures for deferred shading
  {
    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = TextureFormat;

    for(UINT i = 0; i<_countof(m_deferredShadingTextures); i++) {
      memcpy(clearValue.Color, DeferredShadingTexturesClearValues[i], sizeof(DeferredShadingTexturesClearValues[i]));

      ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(TextureFormat, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_deferredShadingTextures[i])
      ));
    }
  }

  //create the descriptors for the shading textures
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = TextureFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = TextureFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_graphicsCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), GP_DescriptorOffset::ShadingTextures, m_cbvSrvUavDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for(int i = 0; i<_countof(m_deferredShadingTextures); i++) {
      m_device->CreateShaderResourceView(m_deferredShadingTextures[i].Get(), &srvDesc, srvHandle);
      srvHandle.Offset(1, m_cbvSrvUavDescriptorSize);

      m_device->CreateRenderTargetView(m_deferredShadingTextures[i].Get(), &rtvDesc, rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
  }

  // Create the vertex buffers.
  ComPtr<ID3D12Resource> groundImmediateVertexBuffer;
  ComPtr<ID3D12Resource> finalQuadImmediateVertexBuffer;
  {
    // Define the geometry for a triangle.
    Vertex triangleVertices[] = {
        { { -5.0f,  0.0f,  5.f, }, { 0.0f, 1.0f }, { 0.f, 1.f, 0.f, } },
        { {  5.0f,  0.0f,  5.f, }, { 1.0f, 1.0f }, { 0.f, 1.f, 0.f, } },
        { { -5.0f,  0.0f, -5.f, }, { 0.0f, 0.0f }, { 0.f, 1.f, 0.f, } },
        { {  5.0f,  0.0f, -5.f, }, { 1.0f, 0.0f }, { 0.f, 1.f, 0.f, } },
    };

    UINT vertexBufferSize = sizeof(triangleVertices);

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&m_vertexInput.ground.buffer)));

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&groundImmediateVertexBuffer)
    ));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = triangleVertices;
    subresourceData.RowPitch = vertexBufferSize;
    subresourceData.SlicePitch = subresourceData.RowPitch;

    UpdateSubresources(m_commandList.Get(),
      m_vertexInput.ground.buffer.Get(), groundImmediateVertexBuffer.Get(),
      0, 0, 1, &subresourceData);

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexInput.ground.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    //Initialize the vertex buffer view.
    m_vertexInput.ground.bufferView.BufferLocation = m_vertexInput.ground.buffer->GetGPUVirtualAddress();
    m_vertexInput.ground.bufferView.StrideInBytes = sizeof(Vertex);
    m_vertexInput.ground.bufferView.SizeInBytes = vertexBufferSize;
  }
  {
    XMFLOAT2 finalQuadVertices[] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    UINT bufferSize = sizeof(finalQuadVertices);

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&m_vertexInput.finalQuad.buffer)));

    ThrowIfFailed(m_device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&finalQuadImmediateVertexBuffer)
    ));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = finalQuadVertices;
    subresourceData.RowPitch = bufferSize;
    subresourceData.SlicePitch = subresourceData.RowPitch;

    UpdateSubresources(m_commandList.Get(),
      m_vertexInput.finalQuad.buffer.Get(), finalQuadImmediateVertexBuffer.Get(),
      0, 0, 1, &subresourceData);

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexInput.finalQuad.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    //Initialize the vertex buffer view.
    m_vertexInput.finalQuad.bufferView.BufferLocation = m_vertexInput.finalQuad.buffer->GetGPUVirtualAddress();
    m_vertexInput.finalQuad.bufferView.StrideInBytes = sizeof(XMFLOAT2);
    m_vertexInput.finalQuad.bufferView.SizeInBytes = bufferSize;    
  }

  // Command lists are created in the recording state, but there is nothing
  // to record yet. The main loop expects it to be closed, so close it now.
  ThrowIfFailed(m_commandList->Close());
  ThrowIfFailed(m_computeCommandList->Close());

  // Create synchronization objects and wait until assets have been uploaded to the GPU.
  {
    ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence)));
    ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeSyncFence)));
    m_fenceValues[m_frameIndex]++;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(m_fenceEvent == nullptr)
    {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    m_computeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(m_computeFenceEvent == nullptr) {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    
    ID3D12CommandList* ppCommandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    WaitForGpu();

    delete latticeBufferData;
  }
}

// Update frame-based values.
void Simulation1::OnUpdate(){}

// Render the scene.
void Simulation1::OnRender(){
  PopulateCommandList();

  if(!freeze) {
    ID3D12CommandList* computeCommandLists[] = {m_computeCommandList.Get()};

    m_computeQueue->ExecuteCommandLists(_countof(computeCommandLists), computeCommandLists);
    m_computeQueue->Signal(m_computeFence.Get(), m_fenceValues[m_frameIndex]);
    m_commandQueue->Wait(m_computeFence.Get(), m_fenceValues[m_frameIndex]);
  }
  ID3D12CommandList* ppCommandLists[] = {m_commandList.Get()};
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  ThrowIfFailed(m_swapChain->Present(1, 0));

  MoveToNextFrame();
}

void Simulation1::OnDestroy(){
  // Ensure that the GPU is no longer referencing resources that are about to be
  // cleaned up by the destructor.
  WaitForGpu();

  CloseHandle(m_fenceEvent);
  CloseHandle(m_computeFenceEvent);
}

void Simulation1::PopulateCommandList(){
  
  //Compute Program Calls
  if(!freeze) {
    ThrowIfFailed(m_computeCommandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_computeCommandList->Reset(m_computeCommandAllocators[m_frameIndex].Get(), m_computePSO.Get()));

    m_computeCommandList->SetComputeRootSignature(m_computeSignature.Get());
    m_computeCommandList->SetPipelineState(m_computePSO.Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_computeCbvSrvUavHeap.Get()};
    m_computeCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    for(UINT run = 0; run < ComputeRuns; run++) {    
      switch(run) {
        case 1:
        {
          UINT counterOffset = offsetof(IndirectCommand, drawArguments.VertexCountPerInstance);
          m_computeCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indirectDrawBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));

          m_computeCommandList->CopyBufferRegion(m_indirectDrawBuffer.Get(), counterOffset, m_indirectDrawBufferReset.Get(), counterOffset, sizeof(IndirectCommand::drawArguments.VertexCountPerInstance));

          m_computeCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indirectDrawBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        } break;
      }

      UINT in = LatticeBufferIn;
      UINT out = LatticeBufferOut;

      if(run & 1) {
        std::swap(in, out);
      }

      CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_latticeBuffer[in].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_latticeBuffer[out].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };

      m_computeCommandList->ResourceBarrier(_countof(barriers), barriers);

      ComputeShaderInput csIn;
      csIn.BitPerLatticePointDirection = BitPerLatticePointDirection;
      csIn.BytePerLatticePoint = BytePerLatticePoint;
      csIn.global_X = LatticeWith * LatticePointsPerUnit;
      csIn.global_Y = LatticeHeight * LatticePointsPerUnit;
      csIn.global_Z = LatticeDepth * LatticePointsPerUnit;
      csIn.threshhold = Threshhold;
      csIn.unit_lenght = 1.f / LatticePointsPerUnit;
      csIn.run = run;
      csIn.PointsPerLattice = PointsPerLattice;
      csIn.timestep = TimeStep;

      D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = m_computeCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

      m_computeCommandList->SetComputeRoot32BitConstants(CS_Slots::Constants, sizeof(ComputeShaderInput)/4, &csIn, 0);
      m_computeCommandList->SetComputeRootDescriptorTable(CS_Slots::Table,
        CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, run * ComputeDescriptorsPerRun, m_cbvSrvUavDescriptorSize));

      m_computeCommandList->Dispatch(LatticeWith * LatticePointsPerUnit, LatticeHeight * LatticePointsPerUnit, LatticeWith * LatticePointsPerUnit);
    }
    ThrowIfFailed(m_computeCommandList->Close());
  }


  //Graphics Program Calls 
  {
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_GPSO.visibility.liquid.Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_graphicsCbvSrvUavHeap.Get()};

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    UINT renderTargetIndex = m_swapChain->GetCurrentBackBufferIndex();

    CD3DX12_RESOURCE_BARRIER initialBarriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[renderTargetIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
      CD3DX12_RESOURCE_BARRIER::Transition(m_vertexInput.liquid.buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(m_indirectDrawBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::POS].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::NORMAL].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::COLOR].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    
    m_commandList->ResourceBarrier(_countof(initialBarriers), initialBarriers);
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE constShaderInputDescriptorHandle(m_graphicsCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), GP_DescriptorOffset::Matrices, m_cbvSrvUavDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(GP_Slots::ShaderInputTable, constShaderInputDescriptorHandle);
    
    //------------------------------------Visibility Pass--------------------------------------------

    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    
    //render the liquid
    m_commandList->SetPipelineState(m_GPSO.visibility.liquid.Get());
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexInput.liquid.bufferView);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->ExecuteIndirect(m_commandSignature.Get(), 1, m_indirectDrawBuffer.Get(), 0, nullptr, 0);

    //render the ground
    m_commandList->SetPipelineState(m_GPSO.visibility.ground.Get());
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexInput.ground.bufferView);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_commandList->DrawInstanced(4, 1, 0, 0);

    //----------------------------------Blending Pass--------------------------------------------------

    CD3DX12_CPU_DESCRIPTOR_HANDLE firstTextureHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_rtvDescriptorSize);
   
    m_commandList->OMSetRenderTargets(_countof(m_deferredShadingTextures), &firstTextureHandle, TRUE, &dsvHandle);
    for(UINT i = 0; i < _countof(m_deferredShadingTextures); i++) {
      m_commandList->ClearRenderTargetView(CD3DX12_CPU_DESCRIPTOR_HANDLE(firstTextureHandle, i, m_rtvDescriptorSize), DeferredShadingTexturesClearValues[i], 0, nullptr);
    }

    //render the liquid
    m_commandList->SetPipelineState(m_GPSO.blending.liquid.Get());
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexInput.liquid.bufferView);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->ExecuteIndirect(m_commandSignature.Get(), 1, m_indirectDrawBuffer.Get(), 0, nullptr, 0);
   
    //render the ground
    m_commandList->SetPipelineState(m_GPSO.blending.ground.Get());
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexInput.ground.bufferView);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_commandList->DrawInstanced(4, 1, 0, 0);

    //--------------------------------Deferred Shading Pass----------------------------------------------
    CD3DX12_CPU_DESCRIPTOR_HANDLE finalRtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), FinalRenderTargetOffset + renderTargetIndex, m_rtvDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE shaderTexturesHandle(m_graphicsCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), GP_DescriptorOffset::ShadingTextures, m_cbvSrvUavDescriptorSize);

    CD3DX12_RESOURCE_BARRIER deferredPassBarriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::POS].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::NORMAL].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
      CD3DX12_RESOURCE_BARRIER::Transition(m_deferredShadingTextures[DeferredShadingTexturesIndex::COLOR].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };

    m_commandList->ResourceBarrier(_countof(deferredPassBarriers), deferredPassBarriers);

    m_commandList->OMSetRenderTargets(1, &finalRtvHandle, TRUE, &dsvHandle);
    m_commandList->SetGraphicsRootDescriptorTable(GP_Slots::TextureTable, shaderTexturesHandle);

    m_commandList->SetPipelineState(m_GPSO.deferred.Get());
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexInput.finalQuad.bufferView);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    m_commandList->DrawInstanced(4, 1, 0, 0);
    
    //-------------------------------Clean up----------------------------------------------------------

    CD3DX12_RESOURCE_BARRIER finalBarriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[renderTargetIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
      CD3DX12_RESOURCE_BARRIER::Transition(m_vertexInput.liquid.buffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      CD3DX12_RESOURCE_BARRIER::Transition(m_indirectDrawBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    m_commandList->ResourceBarrier(_countof(finalBarriers), finalBarriers);

    ThrowIfFailed(m_commandList->Close());
  }
}

void Simulation1::WaitForGpu(){
  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));
  ThrowIfFailed(m_computeQueue->Signal(m_computeFence.Get(), m_fenceValues[m_frameIndex]));

  ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
  WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  ThrowIfFailed(m_computeFence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_computeFenceEvent));
  WaitForSingleObjectEx(m_computeFenceEvent, INFINITE, FALSE);

  m_fenceValues[m_frameIndex]++;
}

void Simulation1::OnKeyDown(UINT8 key){

  if(key == 'F') {
    freeze = !freeze;
  }
}

void Simulation1::MoveToNextFrame(){
  const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex() % FrameCount;
  
  if(m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  }

  m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}


//generates the input data for the Lattice Buffer
//returns a pointer to an UINT array which must be deleted by the caller
UINT* Simulation1::generateInitialLatticeBufferData(UINT latticeBufferSize){
  UINT* data = new UINT[latticeBufferSize >> 2];
  UINT ADDRESS_UNIT_SIZE = sizeof(UINT) << 3;

  ZeroMemory(data, latticeBufferSize);
 
  //initialize the lattice buffer with a cube at its center
  //coodinates of the cube
  int x_start = 1 * LatticePointsPerUnit;
  int x_end = 2 * LatticePointsPerUnit;
  int y_start = 0 * LatticePointsPerUnit;
  int y_end = 1 * LatticePointsPerUnit;
  int z_start = 1 * LatticePointsPerUnit;
  int z_end = 2 * LatticePointsPerUnit;

  for(int z = z_start; z < z_end; z++) {
    for(int y = y_start; y < y_end; y++) {
      for(int x = x_start; x < x_end; x++) {
        int starting_bit = (z * LatticePointsPerUnit * LatticePointsPerUnit * LatticeWith * LatticeHeight +
          y * LatticeWith * LatticePointsPerUnit + x) * BytePerLatticePoint;

        starting_bit /= 4;

        UINT* ptr = data + starting_bit;
        int offset = 0;

        for(int dir = 0; dir < LatticePointDirections; dir++) {
          //must not be greater than 2^BitPerLatticePointDirection
          UINT value = SimulationStartValue;
          UINT bits_left = BitPerLatticePointDirection;

          do {
            UINT start = offset;
            UINT end = start + bits_left - 1;

            bits_left = (std::max<int>)(0, end - ADDRESS_UNIT_SIZE + 1);
            
            if(end >= ADDRESS_UNIT_SIZE) {
              end = ADDRESS_UNIT_SIZE - 1;
            }

            UINT mask = ~0;
            mask >>= ADDRESS_UNIT_SIZE - (end - start + 1);

            UINT in_value_shift = bits_left;
            UINT out_value_shift = (ADDRESS_UNIT_SIZE - end - 1);

            mask <<= in_value_shift;

            UINT value_in = value & mask;

            int shift_diff = in_value_shift - out_value_shift;

            if(shift_diff > 0) {
              value_in >>= shift_diff;
              mask >>= shift_diff;
            } else if(shift_diff < 0) {
              value_in <<= (-shift_diff);
              mask <<= (-shift_diff);
            }

            *ptr = ((~mask) & *ptr) | value_in;

            offset = end + 1;
            if(offset == ADDRESS_UNIT_SIZE) {
              ptr++;
              offset = 0;
            }

          } while(bits_left > 0);
        }
      }
    }
  }
  return data;
}