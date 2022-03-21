#include "src/Simulation2/computation.h"
#include "src/Utility/DXSampleHelper.h"


computation::computation()
{		
}

void computation::load_pipeline(ComPtr<ID3D12Device> device){
	//descriptor heaps

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 1;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&command_allocator));
}

void computation::load_assets(ComPtr<ID3D12Device> device, std::shared_ptr<buffer> vertex_buffer){
	{ //root signature
		CD3DX12_DESCRIPTOR_RANGE1 range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		
		CD3DX12_ROOT_PARAMETER1 signature_parameter[2];
		signature_parameter[0].InitAsConstants(sizeof(cs_in) >> 2, CONST_INPUT_REGISTER);
		signature_parameter[1].InitAsDescriptorTable(_countof(range), range);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(_countof(signature_parameter), signature_parameter);

		ComPtr<ID3DBlob> serialized_signature;
		ComPtr<ID3DBlob> signature_error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serialized_signature, &signature_error));
		ThrowIfFailed(device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	{ //pipeline state
		ComPtr<ID3DBlob> computeShader;
		WCHAR path[512];

		GetAssetsPath(path, _countof(path));
	
#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(lstrcatW(path, L"../../src/Simulation2/shader/compute/compute_shader.hlsl"), nullptr, nullptr, "CSMain", "cs_5_1", compileFlags, 0, &computeShader, nullptr));
		
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
		computePsoDesc.pRootSignature = root_signature.Get();
		computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&pipeline_state)));
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

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, command_allocator.Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	command_list->Close();
}


ID3D12CommandList* computation::populate_command_list(float time){
	command_allocator->Reset();
	command_list->Reset(command_allocator.Get(), pipeline_state.Get());

	ID3D12DescriptorHeap* descriptor_heaps[] = {descriptor_heap.Get()};
	command_list->SetComputeRootSignature(root_signature.Get());
	command_list->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);
	
	cs_in in;
	in.time = time;

	command_list->SetComputeRoot32BitConstants(0, sizeof(cs_in) >> 2, &in, 0);
	command_list->SetComputeRootDescriptorTable(1, descriptor_heap->GetGPUDescriptorHandleForHeapStart());

	command_list->Dispatch(1, 1, 1);
	command_list->Close();

	return command_list.Get();
}