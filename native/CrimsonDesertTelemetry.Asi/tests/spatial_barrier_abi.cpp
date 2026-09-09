#define CINTERFACE
#include <d3d12.h>
#include <cstddef>
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Barrier)==80*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Close)==9*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Reset)==10*sizeof(void*));
static_assert(sizeof(D3D12_STATIC_SAMPLER_DESC)==52);
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,SetComputeRootConstantBufferView)==37*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,SetComputeRootUnorderedAccessView)==41*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,SetComputeRootSignature)==29*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Dispatch)==14*sizeof(void*));
