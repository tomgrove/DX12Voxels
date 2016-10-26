#pragma once

#include "DXSample.h"
#include "defines.h"

static const UINT FrameCount = 2;
static const UINT Depth = cDepth;
static const UINT Height = cHeight;
static const UINT Width = cWidth;
static const UINT BrickWidth = cBrickWidth;
static const UINT BrickHeight = cBrickHeight;
static const UINT BrickDepth = cBrickDepth;
static const UINT VoxelsPerBrick = BrickWidth*BrickDepth *BrickHeight;
static const UINT TileX = 1;
static const UINT TileZ = 1;
static const UINT BrickCount = cDepthInBricks*cHeightInBricks*cWidthInBricks;
static const UINT VoxelCount = BrickCount * VoxelsPerBrick;
static const UINT BrickResourceCount = BrickCount * FrameCount;

#pragma pack(push,4)
struct Voxel
{
	UINT mMaterial;
};
#pragma pack(pop)
