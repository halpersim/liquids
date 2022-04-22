#pragma once

#include "..\Utility\d3dx12.h"
#include "..\Utility\DXSampleHelper.h"

using Microsoft::WRL::ComPtr;

namespace my_utils {
	ComPtr<ID3D12Resource> initialize_buffer(ID3D12Device* device, ID3D12GraphicsCommandList* command_list, ID3D12Resource* dest, void* data, UINT size, D3D12_RESOURCE_STATES state_afterwards){
		ComPtr<ID3D12Resource> upload_buffer;
		
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&upload_buffer)));

		D3D12_SUBRESOURCE_DATA data_desc = {};
		data_desc.pData = data;
		data_desc.RowPitch = size;
		data_desc.SlicePitch = data_desc.RowPitch;

		UpdateSubresources(command_list, dest, upload_buffer.Get(), 0, 0, 1, &data_desc);

		command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, state_afterwards));

		return upload_buffer;
	}
}