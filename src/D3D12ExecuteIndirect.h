//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"
#include "defines.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class VoxelTile
{

};

class D3D12ExecuteIndirect : public DXSample
{
public:
	D3D12ExecuteIndirect(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
	virtual void OnKeyDown(UINT8 key);

private:
	static const UINT FrameCount = 2;
	static const UINT Depth		= cDepth;
	static const UINT Height	= cHeight;
	static const UINT Width		= cWidth;
	static const UINT BrickWidth =  cBrickWidth;
	static const UINT BrickHeight = cBrickHeight;
	static const UINT BrickDepth =  cBrickDepth;
	static const UINT VoxelsPerBrick = BrickWidth*BrickDepth *BrickHeight;
	static const UINT TileX = 1;
	static const UINT TileZ = 1;
	static const UINT BrickCount = cDepthInBricks*cHeightInBricks*cWidthInBricks;
	static const UINT VoxelCount = BrickCount * VoxelsPerBrick;
	static const UINT BrickResourceCount = BrickCount * FrameCount;
	static const UINT CommandSizePerFrame;			     // The size of the indirect commands to draw all of the triangles in a single frame.
	static const UINT CommandBufferCounterOffset;		// The offset of the UAV counter in the processed command buffer.
	static const UINT ComputeThreadBlockSize = 128;		// Should match the value in compute.hlsl.
	static const float VoxelHalfWidth;					// The x and y offsets used by the triangle vertices.
	static const float TriangleDepth;					// The z offset used by the triangle vertices.
	static const float CullingCutoff;					// The +/- x offset of the clipping planes in homogenous space [-1,1].
	static const UINT NumTexture = 1;

	struct ViewConstantBuffer
	{
		XMFLOAT4X4 projection;
		XMFLOAT4   tileoffset;
		UINT	   index;
	};

	static const UINT32 ViewInUInt32s = sizeof(ViewConstantBuffer) / sizeof(UINT32);

	// Constant buffer definition.
	struct SceneConstantBuffer
	{
		UINT color;
	};

	// Root constants for the compute shader.
	struct CSRootConstants
	{
		float xOffset;
		float zOffset;
		float cullOffset;
		float commandCount;
	};

#pragma pack(push, 4)
	struct CSCullConstants
	{
		XMFLOAT4X4 projection;
		float	   commandCount;
	};
#pragma pack(pop)

	const UINT CullConstantsInU32 = sizeof(CSCullConstants) / sizeof(UINT);

	// Data structure to match the command signature used for ExecuteIndirect.
#pragma pack(push, 4)
	struct IndirectCommand
	{
		UINT					  index;
		D3D12_DRAW_ARGUMENTS	  drawArguments;
	};
#pragma pack(pop)

	enum VoxOp
	{
		None,
		Mine,
		Place
	};

	// Graphics root signature parameter offsets.
	enum GraphicsRootParameters
	{
		View,
		Cbv,
		Texture,
		GraphicsRootParametersCount
	};

	// Compute root signature parameter offsets.
	enum ComputeRootParameters
	{
		SrvUavTable,
		RootConstants,			// Root constants that give the shader information about the triangle vertices and culling planes.
		ComputeRootParametersCount
	};

	enum CullRootParameters
	{
		SrvTable,
		UavTable,
		CullRootConstants,			// Root constants that give the shader information about the triangle vertices and culling planes.
		CullRootParametersCount
	};

	// CBV/SRV/UAV desciptor heap offsets.
	enum HeapOffsets
	{
		CbvSrvOffset = 0,										// SRV that points to the constant buffers used by the rendering thread.
		CommandsOffset = CbvSrvOffset + 1,									// SRV that points to all of the indirect commands.
		ProcessedCommandsOffset = CommandsOffset + 1,						// UAV that records the commands we actually want to execute.
		CullCommandsOffset = ProcessedCommandsOffset + 1,
		CbvSrvUavDescriptorCountPerFrame = CullCommandsOffset + 1		// 2 SRVs + 1 UAV for the compute shader.
	};

	// Each triangle gets its own constant buffer per frame.
	std::vector<SceneConstantBuffer> m_constantBufferData;
	UINT8* m_pCbvDataBegin;

	ViewConstantBuffer m_View;

	CSRootConstants m_csRootConstants;	// Constants for the compute shader.
	CSCullConstants  m_cullConstants;

	// Pipeline objects.
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;
	D3D12_RECT m_cullingScissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_computeCommandAllocators[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_cullCommandAllocators[FrameCount];
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandQueue> m_computeCommandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12RootSignature> m_computeRootSignature;
	ComPtr<ID3D12RootSignature> m_cullRootSignature;
	ComPtr<ID3D12CommandSignature> m_commandSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;

	UINT m_rtvDescriptorSize;
	UINT m_cbvSrvUavDescriptorSize;
	UINT m_frameIndex;
	UINT m_bufIndex;


	XMFLOAT3 m_Position;
	bool     m_RunCompute;
	VoxOp	 m_VoxOp;

	// Synchronization objects.
	ComPtr<ID3D12Fence> m_fence;
	ComPtr<ID3D12Fence> m_computeFence;
	UINT64 m_fenceValues[FrameCount];
	HANDLE m_fenceEvent;

	// Asset objects.
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12PipelineState> m_computeState;
	ComPtr<ID3D12PipelineState> m_cullState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12GraphicsCommandList> m_computeCommandList;
	ComPtr<ID3D12GraphicsCommandList> m_cullCommandList;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ComPtr<ID3D12Resource> m_depthStencil;
	ComPtr<ID3D12Resource> m_commandBuffer;
	ComPtr<ID3D12Resource> m_processedCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource> m_processedCommandBufferCounterReset;
	ComPtr<ID3D12Resource> m_cullCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource> m_texture;
	
	void LoadPipeline();
	void LoadAssets();
	float GetRandomFloat(float min, float max);
	void PopulateCommandLists();
	void WaitForGpu();
	void MoveToNextFrame();
	XMFLOAT3 GetPositionFromIndex(UINT index) const ;
	XMFLOAT3 GetBrickPositionFromIndex(UINT index) const;
	XMFLOAT3 GetVoxelPositionFromIndex(UINT index) const;

	// We pack the UAV counter into the same buffer as the commands rather than create
	// a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
	// so we pad the command buffer (if necessary) such that the counter will be placed
	// at a valid location in the buffer.
	static inline UINT AlignForUavCounter(UINT bufferSize)
	{
		const UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
		return (bufferSize + (alignment - 1)) & ~(alignment - 1);
	}
};
