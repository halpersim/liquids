#pragma once 

#include "src/utility/stdafx.h"
#include "buffer.h"
#include <memory>


using namespace DirectX;
using Microsoft::WRL::ComPtr;

class computation {

private:
	
	struct cs_in {
		float time;
	};

	static const int CONST_INPUT_REGISTER = 0;

	ComPtr<ID3D12CommandAllocator> command_allocator;
	ComPtr<ID3D12GraphicsCommandList> command_list;
	ComPtr<ID3D12RootSignature> root_signature;
	ComPtr<ID3D12PipelineState> pipeline_state;
	ComPtr<ID3D12DescriptorHeap> descriptor_heap;

	std::shared_ptr<buffer> vertex_buffer;

public:
	computation();

	void load_pipeline(ComPtr<ID3D12Device> device);
	void load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer);

	ID3D12CommandList* populate_command_list(float time);
};