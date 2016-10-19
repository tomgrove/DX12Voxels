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

#define Depth 64
#define Height 64
#define Width 64

struct SceneConstantBuffer
{
	float4 color;
};

struct IndirectCommand
{
	uint  index;
	uint4 drawArguments;
};

cbuffer RootConstants : register(b0)
{
	float xOffset;		// Half the width of the triangles.
	float zOffset;		// The z offset for the triangle vertices.
	float cullOffset;	// The culling plane offset in homogenous space.
	float commandCount;	// The number of commands to be processed.
};

StructuredBuffer<SceneConstantBuffer> cbv				: register(t0);	// SRV: Wrapped constant buffers
StructuredBuffer<IndirectCommand> inputCommands			: register(t1);	// SRV: Indirect commands
AppendStructuredBuffer<IndirectCommand> outputCommands	: register(u0);	// UAV: Processed indirect commands

bool getcolour(uint x, uint y, uint z)
{
	uint index = z * (Depth*Height) + y * Height + x;
	return cbv[index].color.w > 0;
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
		if (cbv[index].color.w == 0)
		{
			return;
		}

		uint z = index / (Depth*Height);
		uint y = (index %  (Depth*Height) ) / (Height);
		uint x = (index % (Depth*Height)) % Width;

		// bit of crude culling 

		if (z > 0 && z < (Depth-1))
		{
			if ( !getcolour(x, y, z - 1 ) ||
				 !getcolour(x, y, z + 1) )
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

		if (y > 0 && y < (Height-1))
		{
			if ( !getcolour(x, y-1, z) ||
				 !getcolour(x, y+1, z))
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

		if (x > 0 && x < (Width-1))
		{
			if ( !getcolour(x-1, y, z) ||
				 !getcolour(x+1, y, z))
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
