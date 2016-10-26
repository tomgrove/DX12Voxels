#pragma once

#include "Definitions.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class VoxelTile
{
	UINT							  m_DescriptorOffset;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12GraphicsCommandList> m_computeCommandList;
	ComPtr<ID3D12GraphicsCommandList> m_cullCommandList;
	ComPtr<ID3D12Resource>			  m_processedCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource>			  m_cullCommandBuffers[FrameCount];
	ComPtr<ID3D12PipelineState>		  m_pipelineState;
	ComPtr<ID3D12PipelineState>		  m_computeState;
	ComPtr<ID3D12PipelineState>		  m_cullState;
};