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

#define threadBlockSize 128

#include "defines.h"

struct SceneConstantBuffer
{
	uint color;
};

struct IndirectCommand
{
	uint  index;
	uint4 drawArguments;
};

cbuffer RootConstants : register(b0)
{
	float commandCount;	// The number of commands to be processed.
};

StructuredBuffer<SceneConstantBuffer> cbv				: register(t0);	// SRV: Wrapped constant buffers
StructuredBuffer<IndirectCommand> inputCommands			: register(t1);	// SRV: Indirect commands
AppendStructuredBuffer<IndirectCommand> outputCommands	: register(u0);	// UAV: Processed indirect commands


bool IsBrickSolid(uint3  InOffset)
{
	uint voxelsPerBrick = cBrickWidth*cBrickHeight*cBrickDepth;
	uint index = InOffset.z * (cWidthInBricks*cHeightInBricks) + InOffset.y * cWidthInBricks + InOffset.x;
	index *= voxelsPerBrick;
	for (uint v = 0; v < voxelsPerBrick; v++ )
	{
		if (cbv[index+v].color == 0)
		{
			return false;
		}
	}

	return true;
}

bool IsBrickEmpty(uint3  InOffset)
{
	uint voxelsPerBrick = cBrickWidth*cBrickHeight*cBrickDepth;
	uint index = InOffset.z * (cWidthInBricks*cHeightInBricks) + InOffset.y * cWidthInBricks + InOffset.x;
	index *= voxelsPerBrick;
	for (uint v = 0; v < voxelsPerBrick; v++)
	{
		if (cbv[index + v].color != 0)
		{
			return false;
		}
	}

	return true;
}

[numthreads(threadBlockSize, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	// Each thread of the CS operates on one of the indirect commands.
	uint index = (groupId.x * threadBlockSize) + groupIndex;

	// Don't attempt to access commands that don't exist if more threads are allocated
	// than commands


	if (index < commandCount)
	{
//		outputCommands.Append(inputCommands[index]);
	//	return;

		uint3 brick;

		brick.z = index /  (cWidthInBricks*cHeightInBricks);
		brick.y = (index % (cWidthInBricks*cHeightInBricks)) / cWidthInBricks;
		brick.x = (index % (cWidthInBricks*cHeightInBricks)) % cWidthInBricks;

		if (IsBrickEmpty(brick))
		{
			return;
		} 

		if (brick.z > 0 && brick.z < (cDepthInBricks-1))
		{
			if ( !IsBrickSolid( brick - uint3(0, 0,1 )) ||
				 !IsBrickSolid( brick + uint3(0, 0,1)) )
			{
				outputCommands.Append(inputCommands[index]);
				return;
			}
		}
		else
		{
			outputCommands.Append(inputCommands[index]);
			return;
		}

		if ( brick.y > 0 && brick.y < (cHeightInBricks-1))
		{
			if ( !IsBrickSolid( brick - uint3(0, 1, 0)) ||
				 !IsBrickSolid( brick + uint3(0, 1, 0)))
			{
			  outputCommands.Append(inputCommands[index]);
			  return;
			}
		}
		else
		{
			outputCommands.Append(inputCommands[index]);
			return;
		}

		if ( brick.x > 0 && brick.x < (cWidthInBricks-1))
		{
			if ( !IsBrickSolid( brick - uint3(1, 0, 0)) ||
				 !IsBrickSolid( brick + uint3(1, 0, 0)))
			{
		     	outputCommands.Append(inputCommands[index]);
			}
		}
		else
		{
			outputCommands.Append(inputCommands[index]);
		}
	}
}
