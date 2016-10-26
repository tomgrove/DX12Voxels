#pragma once

#include "Definitions.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class VoxelTile
{
public:
	VoxelTile() :
		m_DescriptorOffset(0)
	{}

	void Init();

	UINT							  m_DescriptorOffset;
	ComPtr<ID3D12Resource>			  m_processedCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource>			  m_cullCommandBuffers[FrameCount];
};