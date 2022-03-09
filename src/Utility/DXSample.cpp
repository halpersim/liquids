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

#include "stdafx.h"
#include "DXSample.h"

using namespace Microsoft::WRL;

DXSample::DXSample(UINT width, UINT height, std::wstring name) :
  m_width(width),
  m_height(height),
  m_title(name),
  m_useWarpDevice(false){
  WCHAR assetsPath[512];
  GetAssetsPath(assetsPath, _countof(assetsPath));
  m_assetsPath = assetsPath;

  m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
}

DXSample::~DXSample(){}

// Helper function for resolving the full path of assets.
std::wstring DXSample::GetAssetFullPath(LPCWSTR assetName){
  return m_assetsPath + assetName;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_
void DXSample::GetHardwareAdapter(
  IDXGIFactory1* pFactory,
  IDXGIAdapter1** ppAdapter,
  bool requestHighPerformanceAdapter){
  *ppAdapter = nullptr;

  ComPtr<IDXGIAdapter1> current_adapter;
  ComPtr<IDXGIAdapter1> best_adapter;

  ComPtr<IDXGIFactory1> factory1;
  if(SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory1))))
  {
    SIZE_T maxDedicatedVideoMemory = 0;

    for(
      UINT adapterIndex = 0;
      //SUCCEEDED(factory1->EnumAdapterByGpuPreference(
      //  adapterIndex,
      //  requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
      //  IID_PPV_ARGS(&adapter)));
      SUCCEEDED(factory1->EnumAdapters1(adapterIndex, &current_adapter));
      ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
      current_adapter->GetDesc1(&dxgiAdapterDesc1);

      // Check to see if the adapter can create a D3D12 device without actually 
      // creating it. The adapter with the largest dedicated video memory
      // is favored.
      if((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
        SUCCEEDED(D3D12CreateDevice(current_adapter.Get(),
          D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
        dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
      {
        maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
        best_adapter = current_adapter.Get();
      }
    }
  }

  if(best_adapter.Get() == nullptr)
  {
    for(UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &best_adapter)); ++adapterIndex)
    {
      DXGI_ADAPTER_DESC1 desc;
      best_adapter->GetDesc1(&desc);

      if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        // If you want a software adapter, pass in "/warp" on the command line.
        continue;
      }

      // Check to see whether the adapter supports Direct3D 12, but don't create the
      // actual device yet.
      if(SUCCEEDED(D3D12CreateDevice(best_adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
    }
  }

  *ppAdapter = best_adapter.Detach();
}

// Helper function for setting the window's title text.
void DXSample::SetCustomWindowText(LPCWSTR text){
  std::wstring windowText = m_title + L": " + text;
  SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
}

// Helper function for parsing any supplied command line args.
_Use_decl_annotations_
void DXSample::ParseCommandLineArgs(WCHAR* argv[], int argc){
  for(int i = 1; i < argc; ++i)
  {
    if(_wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 ||
      _wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0)
    {
      m_useWarpDevice = true;
      m_title = m_title + L" (WARP)";
    }
  }
}
