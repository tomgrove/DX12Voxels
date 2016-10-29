#pragma once

#include "DXSample.h"
#include "defines.h"

using namespace DirectX;

// CBV/SRV/UAV desciptor heap offsets.
enum TileHeapOffsets
{
	VoxelBufferOffset					= 0,								// SRV that points to the voxels for the current tile
	DrawCommandsOffset					= VoxelBufferOffset + 1,			// SRV that points to the raw draw commands.
	ProcessedDrawCommandsOffset			= DrawCommandsOffset + 1,			// UAV that records the commands used to draw the non-enclosed bricks
	CounterOffset						= ProcessedDrawCommandsOffset + 1,	// CBV holding the count of the non-enclosed bricks
	CulledDrawCommandsOffset			= CounterOffset + 1,				// UAV that records the frustum visib,le bricks
 	DescriptorCountPerFrame				= CulledDrawCommandsOffset + 1		// total
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

// Root constants for the compute shader.
struct ComputeRootConstants
{
	float CommandCount;
};

static const UINT ComputeRootConstantsInU32s = sizeof(ComputeRootConstants) / sizeof(UINT32);

#pragma pack(push, 4)
struct DrawVoxelCommand
{
	UINT					  Data;
	D3D12_DRAW_ARGUMENTS	  DrawArguments;
};
#pragma pack(pop)

#pragma pack(push,4)
struct Voxel
{
	UINT mMaterial;
};
#pragma pack(pop)

struct ViewParams
{
	XMFLOAT4X4 mProjection;
	UINT       mFrame;
};

#pragma pack(push, 4)
struct CSCullConstants
{
	XMFLOAT4X4 projection;
};
#pragma pack(pop)
const UINT CullConstantsInU32 = sizeof(CSCullConstants) / sizeof(UINT);

static const UINT FrameCount = 2;
static const UINT Depth = cDepth;
static const UINT Height = cHeight;
static const UINT Width = cWidth;
static const UINT BrickWidth = cBrickWidth;
static const UINT BrickHeight = cBrickHeight;
static const UINT BrickDepth = cBrickDepth;
static const UINT VoxelsPerBrick = BrickWidth*BrickDepth *BrickHeight;
static const UINT TileX = 1;
static const UINT TileY = 1;
static const UINT TileZ = 1;
static const UINT BrickCount = cDepthInBricks*cHeightInBricks*cWidthInBricks;
static const UINT VoxelCount = BrickCount * VoxelsPerBrick;
static const UINT BrickResourceCount = BrickCount * FrameCount;
static const UINT CommandSizePerTile = BrickCount * sizeof(DrawVoxelCommand);
static const UINT NumTexture = 1;
static const UINT TileDescriptorStart = NumTexture;
static const UINT ComputeThreadBlockSize = 128;		// Should match the value in compute.hlsl.

// We pack the UAV counter into the same buffer as the commands rather than create
// a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
// so we pad the command buffer (if necessary) such that the counter will be placed
// at a valid location in the buffer.
static inline UINT AlignForUavCounter(UINT bufferSize)
{
	const UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
	return (bufferSize + (alignment - 1)) & ~(alignment - 1);
}