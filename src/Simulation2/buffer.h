#pragma once

#include "src/utility/stdafx.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class buffer {
private:
	ComPtr<ID3D12Resource> m_ptr;
	UINT m_size;
	UINT m_stride;
	UINT m_num_elements;

	D3D12_RESOURCE_STATES m_state;

public:

	buffer(ComPtr<ID3D12Resource> ptr, UINT num_elements, UINT stride, D3D12_RESOURCE_STATES state);

	UINT size() const;
	UINT stride() const;
	UINT num_elements() const;

	ID3D12Resource* get();

	CD3DX12_RESOURCE_BARRIER transition(D3D12_RESOURCE_STATES new_state);
};