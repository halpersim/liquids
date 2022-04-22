#include "rendering.h"
#include "src/Utility/DXSampleHelper.h"


const XMVECTOR rendering::EYE = XMVectorSet(0.f, 0.f, -20.f, 1.f);

rendering::rendering(UINT width, UINT height) :
	width(width),
	height(height),
	aspect_ratio(float(width)/height),
	viewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
	scissor_rect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	rtv_descriptor_size(0),
	vertex_buffer_view(),
	shader_input()
{}

void rendering::load_pipeline(ComPtr<ID3D12Device> device){
	D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};

	rtv_desc.NumDescriptors = FRAME_COUNT;
	rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap));
	
	for(UINT i = 0; i<FRAME_COUNT; i++) {
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator[i]));
	}

	rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {};
	dsv_desc.NumDescriptors = 1;
	dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap)));
}

void rendering::load_assets(ComPtr<ID3D12Device> device, ComPtr<IDXGISwapChain3> swap_chain, std::shared_ptr<buffer> vertex_buffer){
	{ // root signature
		CD3DX12_ROOT_PARAMETER1 root_parameter[1];
		root_parameter[0].InitAsConstants(sizeof(ShaderInput) >> 2, 0);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(_countof(root_parameter), root_parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serialized_signature;
		ComPtr<ID3DBlob> signature_error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serialized_signature, &signature_error));
		ThrowIfFailed(device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	{ //pipeline state
		ComPtr<ID3DBlob> vs;
		ComPtr<ID3DBlob> gs;
		ComPtr<ID3DBlob> ps;

		WCHAR path[512];
		GetAssetsPath(path, _countof(path));
		lstrcatW(path, L"../../src/Simulation2/shader/graphics/triangles.hlsl");


#if defined(_DEBUG)
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(path, nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vs, nullptr));
		ThrowIfFailed(D3DCompileFromFile(path, nullptr, nullptr, "GSMain", "gs_5_1", compileFlags, 0, &gs, nullptr));
		ThrowIfFailed(D3DCompileFromFile(path, nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &ps, nullptr));

		D3D12_INPUT_ELEMENT_DESC input_layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = {input_layout, _countof(input_layout)};
		psoDesc.pRootSignature = root_signature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
		psoDesc.GS = CD3DX12_SHADER_BYTECODE(gs.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
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
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline_state)));
	}

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator[0].Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	command_list->Close();
	
	this->vertex_buffer = vertex_buffer;

	vertex_buffer_view.BufferLocation = vertex_buffer->get()->GetGPUVirtualAddress();
	vertex_buffer_view.SizeInBytes = vertex_buffer->size();
	vertex_buffer_view.StrideInBytes = vertex_buffer->stride();
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart(), 0, 0);

	for(UINT i = 0; i<FRAME_COUNT; i++) {
		ThrowIfFailed(swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_target[i])));
		device->CreateRenderTargetView(render_target[i].Get(), nullptr, rtv_handle);
		rtv_handle.Offset(1, rtv_descriptor_size);
	}

	// Create the depth Buffer
	{
		D3D12_CLEAR_VALUE optimizedClearValue = {};
		optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		optimizedClearValue.DepthStencil = {1.f, 0};

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&optimizedClearValue,
			IID_PPV_ARGS(&depth_buffer)
		));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;
		dsv.Flags = D3D12_DSV_FLAG_NONE;

		device->CreateDepthStencilView(depth_buffer.Get(), &dsv, dsv_heap->GetCPUDescriptorHandleForHeapStart());
	}

	{ //initialise Matrices
		XMMATRIX world = XMMatrixIdentity();
		XMMATRIX view = XMMatrixLookAtLH(EYE, XMVectorSet(2.5f, 2.5f, 2.5f, 1.f), XMVectorSet(0.f, 1.f, 0.f, 0.f));
		XMMATRIX projection = XMMatrixPerspectiveFovLH(1.5708f, aspect_ratio, 0.1f, 100.f);

		shader_input.mvp = XMMatrixMultiply(world, XMMatrixMultiply(view, projection));
	}
}

ID3D12CommandList* rendering::populate_command_list(UINT backbuffer_index){
	command_allocator[backbuffer_index]->Reset();
	command_list->Reset(command_allocator[backbuffer_index].Get(), pipeline_state.Get());

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart(), backbuffer_index, rtv_descriptor_size);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap->GetCPUDescriptorHandleForHeapStart());

	command_list->SetGraphicsRootSignature(root_signature.Get());
	command_list->RSSetViewports(1, &viewport);
	command_list->RSSetScissorRects(1, &scissor_rect);
	command_list->SetGraphicsRoot32BitConstants(0, sizeof(ShaderInput) >> 2, &shader_input, 0);

	CD3DX12_RESOURCE_BARRIER before_barriers[] = {
		vertex_buffer->transition(D3D12_RESOURCE_STATE_GENERIC_READ),
		CD3DX12_RESOURCE_BARRIER::Transition(render_target[backbuffer_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
	};

	command_list->ResourceBarrier(_countof(before_barriers), before_barriers);

	command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
	const FLOAT bg[] = {0.1f, 0.1f, 0.1f, 1.f};

	command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	command_list->ClearRenderTargetView(rtv_handle, bg, 0, nullptr);
	command_list->SetPipelineState(pipeline_state.Get());
	command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	command_list->DrawInstanced(frame_constants::PARTICLE_COUNT, 1, 0, 0);

	CD3DX12_RESOURCE_BARRIER after_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(render_target[backbuffer_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
		vertex_buffer->transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};

	command_list->ResourceBarrier(_countof(after_barriers), after_barriers);

	command_list->Close();

	return command_list.Get();
}