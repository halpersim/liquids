#include "src/Simulation2/computation.h"
#include "src/Utility/DXSampleHelper.h"
#include "my_utils.h"

#define MAP_BUFFERS

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
	
	memcpy(constants.gravity, frame_constants::GRAVITY, sizeof(frame_constants::GRAVITY));
	memcpy(constants.boundary, frame_constants::SIMULATION_BOX_BOUNDARY, sizeof(frame_constants::SIMULATION_BOX_BOUNDARY));

	constants.particle_radius = frame_constants::PARTICLE_RADIUS;
	memcpy(constants.grid_size, frame_constants::GRID_SIZE, sizeof(frame_constants::GRID_SIZE));
}

void computation::load_pipeline(ComPtr<ID3D12Device> device){
	//descriptor heaps

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	
	desc.NumDescriptors = 0;
	std::for_each(DESCRIPTOR_PER_PASS.begin(), DESCRIPTOR_PER_PASS.end(), [&desc](int n) {desc.NumDescriptors += n; });
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&command_allocator));

	srv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

ID3D12CommandList* computation::load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer, std::vector<ComPtr<ID3D12Resource>>& upload_heaps){
	{ //root signature
		CD3DX12_DESCRIPTOR_RANGE1 grid_range[3];
		grid_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		grid_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		grid_range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_DESCRIPTOR_RANGE1 sort_range[1];
		sort_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_DESCRIPTOR_RANGE1 table_range[3];
		table_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		table_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		table_range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_DESCRIPTOR_RANGE1 density_range[3];
		density_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		density_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		density_range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_DESCRIPTOR_RANGE1 force_range[3];
		force_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		force_range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		force_range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		
		CD3DX12_ROOT_PARAMETER1 signature_parameter[6];
		signature_parameter[SHADER_ORDER::CREATE_GRID].InitAsDescriptorTable(_countof(grid_range), grid_range);
		signature_parameter[SHADER_ORDER::SORT].InitAsDescriptorTable(_countof(sort_range), sort_range);
		signature_parameter[SHADER_ORDER::CREATE_TABLE].InitAsDescriptorTable(_countof(table_range), table_range);
		signature_parameter[SHADER_ORDER::DENSITY_EVALUATION].InitAsDescriptorTable(_countof(density_range), density_range);
		signature_parameter[SHADER_ORDER::APPLY_FORCES].InitAsDescriptorTable(_countof(force_range), force_range);
		signature_parameter[SORTING_CONTANTS_SLOT].InitAsConstants(sizeof(SortParameters) >> 2, 1, 0);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(_countof(signature_parameter), signature_parameter);

		ComPtr<ID3DBlob> serialized_signature;
		ComPtr<ID3DBlob> signature_error;
		
		HRESULT result = D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serialized_signature, &signature_error);

		if(FAILED(result)) {
			OutputDebugStringA(reinterpret_cast<char*>(signature_error->GetBufferPointer())); 
			ThrowIfFailed(result);
		}		
		
		ThrowIfFailed(device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	{ //pipeline state
		load_shader(device.Get(), root_signature.Get(), &pso.create_grid, L"../../src/Simulation2/shader/compute/create_grid.hlsl");
		load_shader(device.Get(), root_signature.Get(), &pso.sort, L"../../src/Simulation2/shader/compute/sort.hlsl");
		load_shader(device.Get(), root_signature.Get(), &pso.create_table, L"../../src/Simulation2/shader/compute/create_table.hlsl");
		load_shader(device.Get(), root_signature.Get(), &pso.density, L"../../src/Simulation2/shader/compute/density_evaluation.hlsl");
		load_shader(device.Get(), root_signature.Get(), &pso.force, L"../../src/Simulation2/shader/compute/apply_forces.hlsl");
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

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, command_allocator.Get(), pso.create_grid.Get(), IID_PPV_ARGS(&command_list)));
	
#ifdef MAP_BUFFERS
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
#else
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
#endif

	ThrowIfFailed(device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(frame_constants::PARTICLE_COUNT * sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&density_buffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(frame_constants::PARTICLE_COUNT * sizeof(float) * 3, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&velocity_buffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(SHADER_INPUT_SIZE),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&constant_buffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(frame_constants::PARTICLE_COUNT * sizeof(UINT) * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&grid_buffer)
	));

	ThrowIfFailed(device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(LOOKUP_TABLE_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&lookup_buffer)
	));

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(LOOKUP_TABLE_SIZE),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&lookup_reset_buffer)
	));

#define SET_NAME(x) x->SetName(L#x);
	SET_NAME(density_buffer);
	SET_NAME(velocity_buffer);
	SET_NAME(constant_buffer);
	SET_NAME(grid_buffer);
	SET_NAME(lookup_buffer);
	SET_NAME(lookup_reset_buffer);
#undef SET_NAME



	UINT* reset_buffer = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(lookup_reset_buffer->Map(0, &readRange, reinterpret_cast<void**>(&reset_buffer)));

	memset(reset_buffer, 0xFFFF, LOOKUP_TABLE_SIZE);

	lookup_reset_buffer->Unmap(0, nullptr);
	//command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(lookup_reset_buffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE));

	float velocity_data[frame_constants::PARTICLE_COUNT * 3];
	ZeroMemory(velocity_data, sizeof(velocity_data));
	
	upload_heaps.push_back(my_utils::initialize_buffer(device.Get(), command_list.Get(), velocity_buffer.Get(), velocity_data, sizeof(velocity_data), D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	float vertex_buffer_data[frame_constants::PARTICLE_COUNT][3];
	float diff = frame_constants::INITIAL_DISPLACEMENT;

	float cube_length_particles = pow(frame_constants::PARTICLE_COUNT, 0.3333333333333333);
	float cube_lenght =  cube_length_particles * diff;
	float start_x = (frame_constants::SIMULATION_BOX_BOUNDARY[0] - cube_lenght) * 0.5;
	float start_z = (frame_constants::SIMULATION_BOX_BOUNDARY[2] - cube_lenght) * 0.5;
	
	int i = 0;
	for(int x = 0; x < cube_length_particles && i < frame_constants::PARTICLE_COUNT; x++) {
		for(int y = 0; y < cube_length_particles && i < frame_constants::PARTICLE_COUNT; y++) {
			for(int z = 0; z < cube_length_particles && i < frame_constants::PARTICLE_COUNT; z++) {
				vertex_buffer_data[i][0] = start_x + x * diff;
				vertex_buffer_data[i][1] = (y + 1) * diff;
				vertex_buffer_data[i][2] = start_z + z * diff;

				i++;
			}
		}
	}
	
	BYTE constants_mem_buffer[SHADER_INPUT_SIZE];
	ZeroMemory(constants_mem_buffer, sizeof(constants_mem_buffer));
	memcpy(constants_mem_buffer, &constants, sizeof(constants));
	
	command_list->ResourceBarrier(1, &vertex_buffer->transition(D3D12_RESOURCE_STATE_COPY_DEST));
	upload_heaps.push_back(my_utils::initialize_buffer(device.Get(), command_list.Get(), vertex_buffer->get(), vertex_buffer_data, sizeof(vertex_buffer_data), D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	upload_heaps.push_back(my_utils::initialize_buffer(device.Get(), command_list.Get(), constant_buffer.Get(), constants_mem_buffer, sizeof(constants_mem_buffer), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	CD3DX12_CPU_DESCRIPTOR_HANDLE heap_handle(descriptor_heap->GetCPUDescriptorHandleForHeapStart());

	descriptor_offset.create_grid = 0.f;

	D3D12_CONSTANT_BUFFER_VIEW_DESC constants_desc = {};
	constants_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
	constants_desc.SizeInBytes = SHADER_INPUT_SIZE;
	
	device->CreateConstantBufferView(&constants_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

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

	D3D12_UNORDERED_ACCESS_VIEW_DESC grid_uav_desc = {};
	grid_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
	grid_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	grid_uav_desc.Buffer.FirstElement = 0;
	grid_uav_desc.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	grid_uav_desc.Buffer.StructureByteStride = sizeof(UINT) * 2;
	grid_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	
	device->CreateUnorderedAccessView(grid_buffer.Get(), nullptr, &grid_uav_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	descriptor_offset.sort = DESCRIPTOR_PER_PASS[0];

	device->CreateUnorderedAccessView(grid_buffer.Get(), nullptr, &grid_uav_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	descriptor_offset.create_table = descriptor_offset.sort + DESCRIPTOR_PER_PASS[1];
	
	device->CreateConstantBufferView(&constants_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	D3D12_SHADER_RESOURCE_VIEW_DESC grid_srv_desc = {};
	grid_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	grid_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	grid_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	grid_srv_desc.Buffer.FirstElement = 0;
	grid_srv_desc.Buffer.NumElements = frame_constants::PARTICLE_COUNT;
	grid_srv_desc.Buffer.StructureByteStride = sizeof(UINT) * 2;
	grid_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	device->CreateShaderResourceView(grid_buffer.Get(), &grid_srv_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	D3D12_UNORDERED_ACCESS_VIEW_DESC table_uav_desc = {};
	table_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
	table_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	table_uav_desc.Buffer.FirstElement = 0;
	table_uav_desc.Buffer.NumElements = frame_constants::GRID_SIZE_FLAT;
	table_uav_desc.Buffer.StructureByteStride = sizeof(UINT);
	table_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	device->CreateUnorderedAccessView(lookup_buffer.Get(), nullptr, &table_uav_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

	descriptor_offset.density = descriptor_offset.create_table + DESCRIPTOR_PER_PASS[2];

	device->CreateConstantBufferView(&constants_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

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

	descriptor_offset.force = descriptor_offset.density + DESCRIPTOR_PER_PASS[2];

	device->CreateConstantBufferView(&constants_desc, heap_handle);
	heap_handle.Offset(1, srv_descriptor_size);

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


void computation::load_shader(ID3D12Device* device, ID3D12RootSignature* root_signature, ComPtrRef<ComPtr<ID3D12PipelineState>> pso, PCWCH filename){
	ComPtr<ID3DBlob> shader;
	WCHAR path[512];
	GetAssetsPath(path, _countof(path));

#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ThrowIfFailed(D3DCompileFromFile(lstrcatW(path, filename), nullptr, nullptr, "CSMain", "cs_5_1", compileFlags, 0, &shader, nullptr));

	D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.pRootSignature = root_signature;
	pso_desc.CS = CD3DX12_SHADER_BYTECODE(shader.Get());

	ThrowIfFailed(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(pso)));
}

ID3D12CommandList* computation::populate_command_list(float time){
	command_allocator->Reset();
	command_list->Reset(command_allocator.Get(), pso.create_grid.Get());

#ifdef MAP_BUFFERS
	float* density_ptr;
	void* ptr1, * ptr2, * ptr3, * ptr4;
	density_buffer->Map(0, nullptr, reinterpret_cast<void**>(&density_ptr));
	velocity_buffer->Map(0, nullptr, &ptr1);
	vertex_buffer->get()->Map(0, nullptr, &ptr2);
	grid_buffer->Map(0, nullptr, &ptr3);
	lookup_buffer->Map(0, nullptr, &ptr4);
	
	density_buffer->Unmap(0, &D3D12_RANGE());
	velocity_buffer->Unmap(0, &D3D12_RANGE());
	vertex_buffer->get()->Unmap(0, &D3D12_RANGE());
	grid_buffer->Unmap(0, &D3D12_RANGE());
	lookup_buffer->Unmap(0, &D3D12_RANGE());
#endif // MAP_BUFFERS
	

	ID3D12DescriptorHeap* descriptor_heaps[] = {descriptor_heap.Get()};
	command_list->SetComputeRootSignature(root_signature.Get());
	command_list->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

	CD3DX12_RESOURCE_BARRIER grid_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer->get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(grid_buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};

	command_list->ResourceBarrier(_countof(grid_barriers), grid_barriers);
	CD3DX12_GPU_DESCRIPTOR_HANDLE grid_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_offset.create_grid, srv_descriptor_size);
	command_list->SetComputeRootDescriptorTable(SHADER_ORDER::CREATE_GRID, grid_pass_handle);
	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);
	
	command_list->SetPipelineState(pso.sort.Get());
	CD3DX12_GPU_DESCRIPTOR_HANDLE sort_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_offset.sort, srv_descriptor_size);
	command_list->SetComputeRootDescriptorTable(SHADER_ORDER::SORT, sort_pass_handle);
	
	SortParameters params;
	params.max_idx = frame_constants::PARTICLE_COUNT;

	int max_step = static_cast<int>(std::ceil(std::log2(frame_constants::PARTICLE_COUNT / 2)));
	int threads_needed = frame_constants::PARTICLE_COUNT / 2 + frame_constants::PARTICLE_COUNT % 2;
	
	//max_step++;
	for(int step = 0; step <= max_step; step++){
		params.global_stepsize = step;
		int cur_step = step + 1;
		
		// if the steps get too big, synchronise globally
		do {
			cur_step--;

			params.local_stepsize = cur_step;

			command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(grid_buffer.Get()));
			command_list->SetComputeRoot32BitConstants(SORTING_CONTANTS_SLOT, sizeof(SortParameters) >> 2, &params, 0);
			command_list->Dispatch(threads_needed / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);

		} while((1 << cur_step) >= frame_constants::COMPUTE_SHADE_GROUP_SIZE);
	}

	command_list->SetPipelineState(pso.create_table.Get());
	CD3DX12_RESOURCE_BARRIER create_table_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(grid_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(lookup_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
	};

	command_list->ResourceBarrier(_countof(create_table_barriers), create_table_barriers);
	command_list->CopyBufferRegion(lookup_buffer.Get(), 0, lookup_reset_buffer.Get(), 0, LOOKUP_TABLE_SIZE);
	command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(lookup_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	CD3DX12_GPU_DESCRIPTOR_HANDLE table_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_offset.create_table, srv_descriptor_size);
	command_list->SetComputeRootDescriptorTable(SHADER_ORDER::CREATE_TABLE, table_pass_handle);
	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);

	command_list->SetPipelineState(pso.density.Get());
	CD3DX12_RESOURCE_BARRIER density_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(density_buffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	command_list->ResourceBarrier(_countof(density_barriers), density_barriers);

	CD3DX12_GPU_DESCRIPTOR_HANDLE density_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_offset.density, srv_descriptor_size);
	command_list->SetComputeRootDescriptorTable(SHADER_ORDER::DENSITY_EVALUATION, density_pass_handle);
	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);

	command_list->SetPipelineState(pso.force.Get());
	CD3DX12_RESOURCE_BARRIER force_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer->get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(density_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
	};
	command_list->ResourceBarrier(_countof(force_barriers), force_barriers);
	
	CD3DX12_GPU_DESCRIPTOR_HANDLE force_pass_handle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_offset.force, srv_descriptor_size);

	command_list->SetComputeRootDescriptorTable(SHADER_ORDER::APPLY_FORCES, force_pass_handle);

	command_list->Dispatch(frame_constants::PARTICLE_COUNT / frame_constants::COMPUTE_SHADE_GROUP_SIZE, 1, 1);
	command_list->Close();

	return command_list.Get();
}