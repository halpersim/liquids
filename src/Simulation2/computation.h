#pragma once 

#include "src/utility/stdafx.h"
#include "buffer.h"
#include "frame_constants.h"


#include <memory>
#include <array>


using namespace DirectX;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Details::ComPtrRef;

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
		float particle_radius;
		UINT patting;
		UINT grid_size[3];
	};

	struct SortParameters {
		UINT global_stepsize;
		UINT local_stepsize;
		UINT max_idx;
	};

	enum SHADER_ORDER : UINT {
		CREATE_GRID = 0,
		SORT,
		DENSITY_EVALUATION,
		APPLY_FORCES,
	};

	static constexpr UINT SORTING_CONTANTS_SLOT = APPLY_FORCES + 1;

	static constexpr std::array<int, 4> DESCRIPTOR_PER_PASS = {3, 1, 3, 4};

	static constexpr int SHADER_INPUT_SIZE = (sizeof(SimulationConstants) & (~0xFF)) + 0x100;		//constant buffer requires a size divisible by 256 (= 0x100)

	static const int CONST_INPUT_REGISTER = 0;

	ComPtr<ID3D12CommandAllocator> command_allocator;
	ComPtr<ID3D12GraphicsCommandList> command_list;
	ComPtr<ID3D12RootSignature> root_signature;

	struct {
		ComPtr<ID3D12PipelineState> create_grid;
		ComPtr<ID3D12PipelineState> sort;
		ComPtr<ID3D12PipelineState> density;
		ComPtr<ID3D12PipelineState> force;
	} pso;

	ComPtr<ID3D12DescriptorHeap> descriptor_heap;

	std::shared_ptr<buffer> vertex_buffer;

	ComPtr<ID3D12Resource> density_buffer;
	ComPtr<ID3D12Resource> velocity_buffer;
	ComPtr<ID3D12Resource> constant_buffer;
	ComPtr<ID3D12Resource> grid_buffer;
	ComPtr<ID3D12Resource> lookup_buffer;
	
	UINT srv_descriptor_size;

	UINT grid_pass_descriptor_offset;
	UINT sort_pass_descriptor_offset;
	UINT density_pass_descriptor_offset;
	UINT forces_pass_descriptor_offset;
	

	SimulationConstants constants;

public:
	computation();

	void load_pipeline(ComPtr<ID3D12Device> device);
	ID3D12CommandList* load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer, std::vector<ComPtr<ID3D12Resource>>& upload_heaps);

	ID3D12CommandList* populate_command_list(float time);

private:
	void load_shader(ID3D12Device* device, ID3D12RootSignature* root_signature, ComPtrRef<ComPtr<ID3D12PipelineState>> pso, PCWCH filename);
};