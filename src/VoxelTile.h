#pragma once

#include "Definitions.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class SharedResources;

class VoxelTile
{
public:
	VoxelTile(ID3D12Device* device,	 ID3D12DescriptorHeap* descriptorHeap, UINT X, UINT Y, UINT Z ) :
		mDevice( device ),
		mDescriptorHeap( descriptorHeap),
		mIndex( Z * ( TileX * TileY ) + Y * TileY + X ),
		mDescriptorOffset( mIndex *  DescriptorCountPerFrame),
		mDirty( true ),
		mBufferIndex(0)
	{}

	void Init( SharedResources* Shared );
	void AppendEnclosingWork(ID3D12GraphicsCommandList* CommandList, SharedResources* Shared);
	void AppendCullingWork(ID3D12GraphicsCommandList* CommandList, SharedResources* Shared, ViewParams& View );
	void AppendRenderingWork(ID3D12GraphicsCommandList* CommandList);

	ID3D12Device*					  mDevice;
	ID3D12DescriptorHeap*			  mDescriptorHeap;
	UINT						      mIndex;
	UINT							  mDescriptorOffset;
	ComPtr<ID3D12Resource>			  m_processedCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource>			  m_cullCommandBuffers[FrameCount];
	bool							  mDirty;
	UINT							  mBufferIndex;
private:
	void CreateBuffers();
	void CreateVoxelView(SharedResources* Shared);
};