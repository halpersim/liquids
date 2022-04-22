#pragma once 

#include "src/utility/stdafx.h"
#include "buffer.h"
#include "frame_constants.h"
#include <memory>


using namespace DirectX;
using Microsoft::WRL::ComPtr;

class computation {

private:
	
	struct SimulationConstants{
		float smoothing_radius;
		float density_kernel_constant;
		float pressure_kernel_constant;
		float pressure_constant;
		float viscosity_constant;
		float timestep;
		float reference_density;
		UINT particle_count;
		float gravity[3];
		float boundary[3];
	};

	struct ROOT_SLOTS {
		constexpr static UINT CONSTANTS = 0;
		constexpr static UINT DENSITY = 1;
		constexpr static UINT FORCES = 2;
	};

	static const int CONST_INPUT_REGISTER = 0;

	ComPtr<ID3D12CommandAllocator> command_allocator;
	ComPtr<ID3D12GraphicsCommandList> command_list;
	ComPtr<ID3D12RootSignature> root_signature;
	ComPtr<ID3D12PipelineState> density_pso;
	ComPtr<ID3D12PipelineState> force_pso;

	ComPtr<ID3D12DescriptorHeap> descriptor_heap;

	std::shared_ptr<buffer> vertex_buffer;

	ComPtr<ID3D12Resource> density_buffer;
	ComPtr<ID3D12Resource> velocity_buffer;
	
	UINT srv_descriptor_size;

	UINT density_pass_descriptor_offset;
	UINT forces_pass_descriptor_offset;

	SimulationConstants constants;

public:
	computation();

	void load_pipeline(ComPtr<ID3D12Device> device);
	ID3D12CommandList* load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer, std::vector<ComPtr<ID3D12Resource>>& upload_heaps);

	ID3D12CommandList* populate_command_list(float time);
};