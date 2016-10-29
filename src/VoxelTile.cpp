#include "stdafx.h"
#include "VoxelTile.h"
#include "Shared.h"

static UINT CounterBufferOffset = AlignForUavCounter(CommandSizePerTile);

void VoxelTile::Init( SharedResources* Shared )
{
	CreateBuffers();
	CreateVoxelView(Shared);
}

void VoxelTile::AppendEnclosingWork(ID3D12GraphicsCommandList* CommandList, SharedResources* Shared)
{
	if (!mDirty)
	{
		return;
	}

	UINT increment = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	UINT Offset = mDescriptorOffset + mBufferIndex * DescriptorCountPerFrame;
	D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	CommandList->SetComputeRootDescriptorTable(
		SrvUavTable,
		CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, VoxelBufferOffset + Offset, increment));

	ComputeRootConstants rootConstants;
	rootConstants.CommandCount = BrickCount;

	CommandList->SetComputeRoot32BitConstants(RootConstants, ComputeRootConstantsInU32s, reinterpret_cast<void*>(&rootConstants), 0);

	// Reset the UAV counter for this frame.
	CommandList->CopyBufferRegion(m_processedCommandBuffers[mBufferIndex].Get(), CounterOffset, Shared->mCounterReset.Get(), 0, sizeof(UINT));

	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffers[mBufferIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		CommandList->ResourceBarrier(1, &barrier);
	}

	CommandList->Dispatch(static_cast<UINT>(ceil(BrickCount / float(ComputeThreadBlockSize))), 1, 1);

	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffers[mBufferIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		CommandList->ResourceBarrier(1, &barrier);
	}
}

void VoxelTile::AppendCullingWork(ID3D12GraphicsCommandList* CommandList, SharedResources* Shared, ViewParams& View )
{
	UINT increment = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	UINT uavFrameDescriptorOffset = mDescriptorOffset + View.mFrame * DescriptorCountPerFrame;
	UINT srvFrameDescriptorOffset = mDescriptorOffset +  mBufferIndex * DescriptorCountPerFrame;

	D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	/*m_cullCommandList->SetComputeRootSignature(m_cullRootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
	m_cullCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	*/

	CommandList->SetComputeRootDescriptorTable(
		SrvTable,
		CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, ProcessedDrawCommandsOffset + srvFrameDescriptorOffset, increment));

	CommandList->SetComputeRootDescriptorTable(
		UavTable,
		CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, CulledDrawCommandsOffset + uavFrameDescriptorOffset, increment ));

	CSCullConstants rootConstants;
	rootConstants.projection = View.mProjection;

	CommandList->SetComputeRoot32BitConstants(CullRootConstants, CullConstantsInU32, reinterpret_cast<void*>(&rootConstants), 0);

	// Reset the UAV counter for this frame.
	CommandList->CopyBufferRegion(m_cullCommandBuffers[View.mFrame].Get(), CounterOffset, Shared->mCounterReset.Get(), 0, sizeof(UINT));

	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cullCommandBuffers[View.mFrame].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		CommandList->ResourceBarrier(1, &barrier);
	}

	CommandList->Dispatch(static_cast<UINT>(ceil(BrickCount / float(ComputeThreadBlockSize))), 1, 1);

	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cullCommandBuffers[View.mFrame].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		CommandList->ResourceBarrier(1, &barrier);
	}
}

void VoxelTile::AppendRenderingWork(ID3D12GraphicsCommandList* CommandList)
{

}

void VoxelTile::CreateVoxelView( SharedResources* Shared)
{
	UINT increment = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// compute shader to read from.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = VoxelCount;
	srvDesc.Buffer.StructureByteStride = sizeof(Voxel);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.FirstElement = 0;

	UINT tileOffset = this->mIndex * VoxelCount;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle( mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), VoxelBufferOffset + mDescriptorOffset, increment );
	for (int i = 0; i < FrameCount; i++)
	{
		srvDesc.Buffer.FirstElement = tileOffset + i * VoxelCount;
		mDevice->CreateShaderResourceView( Shared->mVoxels.Get(), &srvDesc, cbvSrvHandle);
		cbvSrvHandle.Offset( DescriptorCountPerFrame, increment);
	}
}

void VoxelTile::CreateBuffers()
{
	UINT increment = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_RESOURCE_DESC commandBufferDesc;
	// Create the unordered access views (UAVs) that store the results of the compute work.
	CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle( mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), ProcessedDrawCommandsOffset + mDescriptorOffset, increment);
	for (UINT frame = 0; frame < FrameCount; frame++)
	{
		// Allocate a buffer large enough to hold all of the indirect commands
		// for a single frame as well as a UAV counter.
		commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( CounterBufferOffset + sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		ThrowIfFailed(mDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&commandBufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_processedCommandBuffers[frame])));

		NAME_D3D12_OBJECT_INDEXED(m_processedCommandBuffers, frame);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = BrickCount;
		uavDesc.Buffer.StructureByteStride = sizeof(DrawVoxelCommand);
		uavDesc.Buffer.CounterOffsetInBytes =CounterOffset;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		mDevice->CreateUnorderedAccessView(
				m_processedCommandBuffers[frame].Get(),
				m_processedCommandBuffers[frame].Get(),
				&uavDesc,
				processedCommandsHandle);

			processedCommandsHandle.Offset(DescriptorCountPerFrame, increment);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsCountHandle(mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), CounterOffset + mDescriptorOffset, increment);
	for (UINT frame = 0; frame < FrameCount; frame++)
	{
		// Create SRVs for the command buffers.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = 1;
		srvDesc.Buffer.StructureByteStride = sizeof(UINT);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.FirstElement = CounterBufferOffset / sizeof(UINT);

		mDevice->CreateShaderResourceView(m_processedCommandBuffers[frame].Get(), &srvDesc, processedCommandsCountHandle);
		processedCommandsCountHandle.Offset(DescriptorCountPerFrame, increment);
	}

	{
		// Create the unordered access views (UAVs) that store the culled commands
		CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), CulledDrawCommandsOffset + mDescriptorOffset, increment);
		for (UINT frame = 0; frame < FrameCount; frame++)
		{
			// Allocate a buffer large enough to hold all of the indirect commands
			// for a single frame as well as a UAV counter.
			commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( CounterBufferOffset + sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			ThrowIfFailed(mDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&commandBufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_cullCommandBuffers[frame])));

			NAME_D3D12_OBJECT_INDEXED(m_cullCommandBuffers, frame);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = BrickCount;
			uavDesc.Buffer.StructureByteStride = sizeof(DrawVoxelCommand);
			uavDesc.Buffer.CounterOffsetInBytes = CounterBufferOffset;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			mDevice->CreateUnorderedAccessView(
				m_cullCommandBuffers[frame].Get(),
				m_cullCommandBuffers[frame].Get(),
				&uavDesc,
				processedCommandsHandle);

			processedCommandsHandle.Offset( DescriptorCountPerFrame, increment );
		}
	}
}