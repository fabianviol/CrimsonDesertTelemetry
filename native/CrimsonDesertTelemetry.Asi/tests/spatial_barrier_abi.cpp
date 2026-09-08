#define CINTERFACE
#include <d3d12.h>
#include <cstddef>
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Barrier)==80*sizeof(void*));
static_assert(sizeof(D3D12_STATIC_SAMPLER_DESC)==52);
