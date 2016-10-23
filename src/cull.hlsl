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
	float commandCount;	// The number of commands to be processed.
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
	
	brick *= float4(cBrickWidth * 0.1f, cBrickHeight *0.1f, cBrickDepth * 0.1f,1.0f );
	brick += float4(cBrickWidth * 0.1f, cBrickHeight *0.1f, cBrickDepth * 0.1, 0.0  ) / 2.0;
	float4 p = mul(brick, projection);
	float3 clp = p.xyz / (p.w + 0.000001f);

	if (clp.x > 1.0 || clp.x < -1.0 || clp.y > 1.0f || clp.y < -1.0f || clp.z < 0.0 || clp.z > 1.0f )
	{
		return;
	}

	// Don't attempt to access commands that don't exist if more threads are allocated
	// than commands

	if (index < commandCounts[0].count )
	{
		outputCommands.Append(inputCommands[index]);
	}	
}
