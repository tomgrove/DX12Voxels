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

struct IndirectCommand
{
	uint  index;
	uint4 drawArguments;
};

struct CommandCount
{
	uint count;
};

cbuffer RootConstants : register(b0)
{
	float4x4 projection;
};

StructuredBuffer<IndirectCommand> inputCommands			: register(t0);	// SRV: Indirect commands
StructuredBuffer<CommandCount> commandCounts			: register(t1);
AppendStructuredBuffer<IndirectCommand> outputCommands	: register(u0);	// UAV: Processed indirect commands

[numthreads(threadBlockSize, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	// Each thread of the CS operates on one of the indirect commands.
	uint index = (groupId.x * threadBlockSize) + groupIndex;

	uint4 cmdidx =  inputCommands[index].index;

	float4 brick;

	brick.z =  cmdidx / (cWidthInBricks*cHeightInBricks);
	brick.y = (cmdidx % (cWidthInBricks*cHeightInBricks)) / cWidthInBricks;
	brick.x = (cmdidx % (cWidthInBricks*cHeightInBricks)) % cWidthInBricks;
	brick.w = 1.0f;
	
	float3 brickdims = float3(cBrickWidth*cVoxelHalfWidth*2.0, 
		                      cBrickHeight*cVoxelHalfWidth*2.0, 
		                      cBrickDepth*cVoxelHalfWidth*2.0f );

	brick.xyz *= brickdims;
	brick.xyz += brickdims / 2.0;
 

	float4 p = mul(brick, projection);
	float3 clp = p.xyz / (p.w + 0.000001f);
	float3 bounds = brickdims * 2.4f / 2.0f;
	float r = sqrt(dot(bounds.xyz, bounds.xyz));

    r /= (p.w + 0.000001f);

	if (clp.x > (1.0+r) || clp.x < (-1.0-r) || clp.y > (1.0+r) || clp.y < (-1.0f-r) || clp.z < -r || clp.z > 0.9999f )
	{
		return;
	}

	// Don't attempt to access commands that don't exist if more threads are allocated
	// than commands

	IndirectCommand cmd = inputCommands[index];

	if (clp.z > 0.999)
	{
		cmd.drawArguments.y = 6;
		cmd.index |= 0x80000000;
	}

	if (index < commandCounts[0].count )
	{
		outputCommands.Append(cmd);
	}	
}
