#include "src/Simulation2/computation.h"
#include "src/Utility/DXSampleHelper.h"
#include "my_utils.h"

#pragma warning(disable:4244) //double - float missmatch


computation::computation()
{	
	constants.density_kernel_constant = 315.f / (64 * XM_PI * pow(frame_constants::KERNEL_RADIUS, 9));
	constants.particle_count = frame_constants::PARTICLE_COUNT;
	constants.pressure_constant = frame_constants::PRESSURE_CONSTANT;

	constants.pressure_kernel_constant = -45.f / (XM_PI * pow(frame_constants::KERNEL_RADIUS, 6));
	constants.reference_density = frame_constants::REFERENCE_DENSITY;
	constants.smoothing_radius = frame_constants::KERNEL_RADIUS;

	constants.timestep = frame_constants::TIMESTEP;
	constants.viscosity_constant = frame_constants::VISCOSITY_CONSANT;

	constants.gravity[0] = 0.f;
	constants.gravity[1] = -9.81f;
	constants.gravity[2] = 0.f;

	memcpy(constants.boundary, frame_constants::SIMULATION_BOX_BOUNDARY, sizeof(frame_constants::SIMULATION_BOX_BOUNDARY));
}

void computation::load_pipeline(ComPtr<ID3D12Device> device){
	//descriptor heaps

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 5;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&command_allocator));

	srv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

ID3D12CommandList* computation::load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer, std::vector<ComPtr<ID3D12Resource>>& upload_heaps){
	{ //root signature
		CD3DX12_DESCRIPTOR_RANGE1 density_range[2];
		density_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		density_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_DESCRIPTOR_RANGE1 force_range[2];
		force_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		force_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		
		CD3DX12_ROOT_PARAMETER1 signature_parameter[3];
		signature_parameter[ROOT_SLOTS::CONSTANTS].InitAsConstants(sizeof(SimulationConstants) >> 2, CONST_INPUT_REGISTER);
		signature_parameter[ROOT_SLOTS::DENSITY].InitAsDescriptorTable(_countof(density_range), density_range);
		signature_parameter[ROOT_SLOTS::FORCES].InitAsDescriptorTable(_countof(force_range), force_range);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(_countof(signature_parameter), signature_parameter);

		ComPtr<ID3DBlob> serialized_signature;
		ComPtr<ID3DBlob> signature_error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serialized_signature, &signature_error));
		ThrowIfFailed(device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	{ //pipeline state
		ComPtr<ID3DBlob> density_shader;
		ComPtr<ID3DBlob> force_shader;
		WCHAR path[512];

		WCHAR density_path[512];
		WCHAR force_path[512];

		GetAssetsPath(path, _countof(path));
	
		lstrcpyW(density_path, path);
		lstrcpyW(force_path, path);

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(lstrcatW(density_path, L"../../src/Simulation2/shader/compute/density_evaluation.hlsl"), nullptr, nullptr, "CSMain", "cs_5_1", compileFlags, 0, &density_shader, nullptr));
		
		D3D12_COMPUTE_PIPELINE_STATE_DESC density_pso_desc = {};
		density_pso_desc.pRootSignature = root_signature.Get();
		density_pso_desc.CS = CD3DX12_SHADER_BYTECODE(density_shader.Get());

		ThrowIfFailed(device->CreateComputePipelineState(&density_pso_desc, IID_PPV_ARGS(&density_pso)));

		ThrowIfFailed(D3DCompileFromFile(lstrcatW(force_path, L"../../src/Simulation2/shader/compute/apply_forces.hlsl"), nullptr, nullptr, "CSMain", "cs_5_1", compileFlags, 0, &force_shader, nullptr));

		D3D12_COMPUTE_PIPELINE_STATE_DESC force_pso_desc = {};
		force_pso_desc.pRootSignature = root_signature.Get();
		force_pso_desc.CS = CD3DX12_SHADER_BYTECODE(force_shader.Get());

		ThrowIfFailed(device->CreateComputePipelineState(&force_pso_desc, IID_PPV_ARGS(&force_pso)));
	}
	
	this->vertex_buffer = vertex_buffer;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
	uav_desc.Format = DXGI_FORMAT_UNKNOWN;
	uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uav_desc.Buffer.NumElements = vertex_buffer->num_elements();
	uav_desc.Buffer.FirstElement = 0;
	uav_desc.Buffer.StructureByteStride = vertex_buffer->stride();
	uav_desc.Buffer.CounterOffsetInBytes = 0;

	device->CreateUnorderedAccessView(vertex_buffer->get(), nullptr, &uav_desc, descriptor_heap->GetCPUDescriptorHandleForHeapStart());

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, command_allocator.Get(), density_pso.Get(), IID_PPV_ARGS(&command_list)));
	
	ThrowIfFailed(device->CreateCommittedResource(
		//&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		&CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(frame_constants::PARTICLE_COUNT * sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&density_buffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		//&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		&CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(frame_constants::PARTICLE_COUNT * sizeof(float) * 3, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&velocity_buffer)));

	float velocity_data[frame_constants::PARTICLE_COUNT * 3];
	ZeroMemory(velocity_data, sizeof(velocity_data));
	
	upload_heaps.push_back(my_utils::initialize_buffer(device.Get(), command_list.Get(), velocity_buffer.Get(), velocity_data, sizeof(velocity_data), D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	float vertex_buffer_data[frame_constants::PARTICLE_COUNT][3];
	float diff = frame_constants::INITIAL_DISPLACEMENT;
	for(int i = 0; i<frame_constants::PARTICLE_COUNT; i++) {
		vertex_buffer_data[i][0] = diff + (i % 10) * diff;
		vertex_buffer_data[i][1] = diff + ((i % 100) / 10) * diff;
		vertex_buffer_data[i][2] = diff + ((i % 1000) / 100) * diff;
	}
	
	command_list->ResourceBarrier(1, &vertex_buffer->transition(D3D12_RESOURCE_STATE_COPY_DEST));
	upload_heaps.push_back(my_utils::initialize_buffer(device.Get(), command_list.Get(), vertex_buffer->get(), vertex_buffer_data, sizeof(vertex_buffer_data), D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	CD3DX12_CPU_DESCRIPTOR_HANDLE heap_handle(descriptor_heap->GetCPUDescriptorHandleForHeapStart());
	density_pass_descriptor_offset = 0;

	D3D12_SHADER_RESOURCE_VIEW_DESC pos_srv_des = {};
	pos_srv_des.Format = DXGI_FORMAT_UNKNOWN;
	pos_srv_des.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	pos_srv_des.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	pos_srv_des.Buffer.FirstElement = 0;
	pos_srv_des.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	pos_srv_des.Buffer.StructureByteStride = sizeof(float) * 3;
	pos_srv_des.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	device->CreateShaderResourceView(vertex_buffer->get(), &pos_srv_des, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	D3D12_UNORDERED_ACCESS_VIEW_DESC density_uav_desc = {};
	density_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
	density_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	density_uav_desc.Buffer.FirstElement = 0;
	density_uav_desc.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	density_uav_desc.Buffer.StructureByteStride = sizeof(float);
	density_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	device->CreateUnorderedAccessView(density_buffer.Get(), nullptr, &density_uav_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	forces_pass_descriptor_offset = 2;

	D3D12_SHADER_RESOURCE_VIEW_DESC density_srv_desc = {};
	density_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	density_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	density_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	density_srv_desc.Buffer.FirstElement = 0;
	density_srv_desc.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	density_srv_desc.Buffer.StructureByteStride = sizeof(float);
	density_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	device->CreateShaderResourceView(density_buffer.Get(), &density_srv_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	D3D12_UNORDERED_ACCESS_VIEW_DESC velocity_pos_uav_desc = {};
	velocity_pos_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
	velocity_pos_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	velocity_pos_uav_desc.Buffer.FirstElement = 0;
	velocity_pos_uav_desc.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	velocity_pos_uav_desc.Buffer.StructureByteStride = sizeof(float) * 3;
	velocity_pos_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	device->CreateUnorderedAccessView(velocity_buffer.Get(), nullptr, &velocity_pos_uav_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);
	device->CreateUnorderedAccessView(vertex_buffer->get(), nullptr, &velocity_pos_uav_desc, heap_handle);

	command_list->Close();
	return command_list.Get();
}


ID3D12CommandList* computation::populate_command_list(float time){
	command_allocator->Reset();
	command_list->Reset(command_allocator.Get(), density_pso.Get());

	/*
	float* density_ptr;
	void* ptr1, * ptr2;
	density_buffer->Map(0, nullptr, reinterpret_cast<void**>(&density_ptr));
	velocity_buffer->Map(0, nullptr, &ptr1);
	vertex_buffer->get()->Map(0, nullptr, &ptr2);
	
	density_buffer->Unmap(0, &D3D12_RANGE());
	velocity_buffer->Unmap(0, &D3D12_RANGE());
	vertex_buffer->get()->Unmap(0, &D3D12_RANGE());
	*/

	ID3D12DescriptorHeap* descriptor_heaps[] = {descriptor_heap.Get()};
	command_list->SetComputeRootSignature(root_signature.Get());
	command_list->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);
	command_list->SetComputeRoot32BitConstants(ROOT_SLOTS::CONSTANTS, sizeof(SimulationConstants) >> 2, &constants, 0);

	CD3DX12_RESOURCE_BARRIER density_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer->get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(density_buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	command_list->ResourceBarrier(_countof(density_barriers), density_barriers);

	CD3DX12_GPU_DESCRIPTOR_HANDLE density_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), density_pass_descriptor_offset, srv_descriptor_size);
	command_list->SetComputeRootDescriptorTable(ROOT_SLOTS::DENSITY, density_pass_handle);
	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);

	command_list->SetPipelineState(force_pso.Get());
	CD3DX12_RESOURCE_BARRIER force_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer->get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(density_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
	};
	command_list->ResourceBarrier(_countof(force_barriers), force_barriers);
	
	CD3DX12_GPU_DESCRIPTOR_HANDLE force_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), forces_pass_descriptor_offset, srv_descriptor_size);


	command_list->SetComputeRootDescriptorTable(ROOT_SLOTS::FORCES, force_pass_handle);

	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);
	command_list->Close();

	return command_list.Get();
}