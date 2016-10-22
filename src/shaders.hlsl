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
	float4 color;
};

cbuffer ViewConstantBuffer : register(b1)
{
	float4x4 projection;
	float4	 tileoffset;
	uint     index;
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
	float scale = 0.05;
	PSInput result;

	float3 norms[6] = {
		{ 0,0,-1},
		{0,0,1},
		{0,1,0},
		{0,-1,0},
		{-1,0,0},
		{1,0,0}
	};

	float2 uvs[4] = { {0,1}, {1,1}, {0,0}, {1,0} };

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

	float4 offset; 

	offset.z = uint( index / (cWidth*cHeight) );
	offset.y = uint( (index % (cWidth*cHeight))) / (cWidth);
	offset.x = uint( (index % (cWidth*cHeight))) % cWidth;
	offset.w = 0;

	offset *= (scale*2.0f);

	uint id = pid % 6;

	/*float4 boffset;

	uint voxid = pid / 6;

	boffset.z = voxid / 16;
	boffset.y = (voxid % 16) / 4;
	boffset.x = ( voxid % 16) % 4;
	boffset.w = 0;

	boffset *= (Width*scale*2.0);

	offset += boffset;*/

	result.position = mul( verts[vid + id*4]  +  offset + tileoffset, projection);

	float intensity = saturate((16.0f - result.position.z) / 2.0f);
	float3 light = saturate(dot(normalize( float3(1,1,-2) ), norms[id])) * float3(1,1,1);

	float3 blended = (1.0 - intensity) * float3(0.9, 0.9, 1) + intensity * light;
	result.color = float4(blended, 1.0f);
	result.uv.xy = uvs[vid] / 17.0f + cbv[index].color.xy;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return g_texture.Sample( g_sampler, input.uv.xy ) * input.color;
}
