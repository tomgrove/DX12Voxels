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

#include "defines.h"

struct SceneConstantBuffer
{
	uint color;
};

cbuffer ViewConstantBuffer : register(b1)
{
	float4x4 projection;
	float4	 tileoffset;
	uint     indexAndFlag;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);


StructuredBuffer<SceneConstantBuffer> cbv				: register(t0);	// SRV: Wrapped constant buffers

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

PSInput VSMain(uint pid : SV_InstanceID, uint vid : SV_VertexID )
{
	PSInput result;

	uint index = indexAndFlag & 0x7fffffff;
	uint flag =  indexAndFlag & 0x80000000;

	float scale = cVoxelHalfWidth;

	uint voxid = pid / 6;
	uint voxmatidx = index*cBrickWidth*cBrickHeight*cBrickDepth + voxid;

	if (cbv[voxmatidx].color == 0)
	{
		result.position = float4(0, 0, 0, 0);
		return result;
	}

	float3 norms[6] = {
		{ 0,0,-1},
		{0,0,1},
		{0,1,0},
		{0,-1,0},
		{-1,0,0},
		{1,0,0}
	};

	float2 uvs[4] = { {0,0}, {1,0}, {0,1}, {1,1} };

	float4 verts[4 * 6] = {
	
		{ -scale,scale,-scale,1},
		{ scale, scale,-scale,1},
		{ -scale, -scale,-scale,1},
		{ scale, -scale, -scale, 1},

		{ -scale,scale,scale,1 },
		{ -scale,-scale,scale,1 },
		{ scale, scale,scale,1 },
		{ scale, -scale, scale, 1 },

		{ -scale, scale, scale ,1 },
		{ scale, scale, scale, 1},
		{ -scale, scale, -scale, 1 },
		{ scale, scale, -scale, 1 }, 

		{ -scale, -scale, scale ,1 },
		{ -scale, -scale, -scale, 1 },
		{ scale, -scale, scale, 1 },
		{ scale, -scale, -scale, 1 }, 

		{ -scale, -scale, scale, 1},
		{ -scale, scale, scale, 1},
		{ -scale, -scale, -scale, 1},
		{ -scale, scale, -scale,1 }, 

		{ scale, -scale, scale, 1 },
		{ scale, -scale, -scale, 1 },
		{ scale, scale,  scale, 1 },
		{ scale, scale, -scale,1 } 

};

	float4 brick; 

	brick.z = uint( index /  (cWidthInBricks*cHeightInBricks) );
	brick.y = uint( (index % (cWidthInBricks*cHeightInBricks))) / (cWidthInBricks);
	brick.x = uint( (index % (cWidthInBricks*cHeightInBricks))) % cWidthInBricks;
	brick.w = 0;

	brick *= uint4(cBrickWidth, cBrickHeight, cBrickDepth, 0.0);
	brick *= (scale*2.0f);

	uint id = pid % 6;

	float4 voxel;

	voxel.z = voxid / (cBrickWidth*cBrickHeight);
	voxel.y = (voxid % (cBrickWidth*cBrickHeight)) / cBrickWidth;
	voxel.x = ( voxid % (cBrickWidth*cBrickHeight)) % cBrickWidth;
	voxel.w = 0;

	

	voxel *= (scale*2.0);

	voxel.xyz += float3(scale, scale, scale);

	float4 vertex;

	if (flag)
	{
		vertex = verts[vid + id * 4] * float4(cBrickWidth, cBrickHeight, cBrickDepth, 1) +
			float4(cBrickWidth, cBrickHeight, cBrickDepth, 0)  * scale
									   + brick + tileoffset;
	}
	else
	{
		vertex = verts[vid + id * 4] + brick + voxel + tileoffset;
	}

	result.position = mul( vertex, projection);

	float intensity = saturate((16.0f - result.position.z) / 2.0f);
	float3 light = saturate(dot(normalize( float3(1,1,-2) ), norms[id]) + 0.1f) * float3(1,1,1);

	float3 blended = (1.0 - intensity) * float3(0.9, 0.9, 1) + intensity * light;
	result.color = float4(blended, 1.0f);

	uint texid;

	if (id == 2 || id == 3)
	{
		texid = (cbv[voxmatidx].color & 0x0000ff00) >> 8;
	}
	else
	{
		texid = (cbv[voxmatidx].color & 0x000000ff);
	}

	float2 tex = float2(texid % 16, texid / 16) / 16.0f;

	result.uv.xy = uvs[vid] / 17.0f + tex;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return g_texture.Sample( g_sampler, input.uv.xy ) * input.color;
}
