#include "src/Utility/stdafx.h"
#include "src/Simulation1/Simulation1.h"
#include "src/Simulation2/Simulation2.h"

extern "C" {
	__declspec(dllexport) extern const UINT D3D12SKDVersion = 600;
	__declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\";
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow){
	Simulation2 sample(1280, 720, L"Simulation 2");
	return Win32Application::Run(&sample, hInstance, nCmdShow);
}