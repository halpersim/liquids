#pragma once 

#include "src/utility/stdafx.h"
#include "buffer.h"
#include "frame_constants.h"
#include <memory>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace frame_constants;

class rendering {
private:

	struct ShaderInput {
		XMMATRIX mvp;
	};


	ComPtr<ID3D12CommandAllocator> command_allocator[FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList> command_list;
	ComPtr<ID3D12RootSignature> root_signature;
	ComPtr<ID3D12PipelineState> pipeline_state;

	std::shared_ptr<buffer> vertex_buffer;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	
	CD3DX12_VIEWPORT viewport;
	CD3DX12_RECT scissor_rect;

	ComPtr<ID3D12DescriptorHeap> dsv_heap;
	ComPtr<ID3D12Resource> depth_buffer;

	ComPtr<ID3D12DescriptorHeap> rtv_heap;
	ComPtr<ID3D12Resource> render_target[FRAME_COUNT];

	UINT rtv_descriptor_size;

	UINT width;
	UINT height;
	float aspect_ratio;

	ShaderInput shader_input;

	
public:

	rendering(UINT with, UINT height);

	void load_pipeline(ComPtr<ID3D12Device> device);
	void load_assets(ComPtr<ID3D12Device> device, ComPtr<IDXGISwapChain3> swap_chain, std::shared_ptr<buffer> vertex_buffer);

	ID3D12CommandList* populate_command_list(UINT backbuffer_index);
};
