#include "buffer.h"

buffer::buffer(ComPtr<ID3D12Resource> ptr, UINT num_elements, UINT stride, D3D12_RESOURCE_STATES state):
	m_ptr(ptr),
	m_size(num_elements * stride),
	m_stride(stride),
	m_num_elements(num_elements),
	m_state(state)
{}

UINT buffer::size() const{
	return m_size;
}

UINT buffer::stride() const{
	return m_stride;
}

UINT buffer::num_elements() const{
	return m_num_elements;
}

ID3D12Resource* buffer::get(){
	return m_ptr.Get();
}


CD3DX12_RESOURCE_BARRIER buffer::transition(D3D12_RESOURCE_STATES new_state){
	D3D12_RESOURCE_STATES old_state = m_state;

	m_state = new_state;

	return CD3DX12_RESOURCE_BARRIER::Transition(m_ptr.Get(), old_state, new_state);
}