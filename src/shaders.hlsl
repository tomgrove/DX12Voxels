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

#define Depth 64
#define Height 64
#define Width 64

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

	offset.z = index / (Depth*Height);
	offset.y = (index % (Depth*Height)) / (Height);
	offset.x = (index % (Depth*Height)) % Width;
	offset.w = 0;

	offset *= (scale*2.0f);

	result.position = mul( verts[vid + pid*4]  +  offset + tileoffset, projection);

	float intensity = saturate((16.0f - result.position.z) / 2.0f);
	float3 light = saturate(dot(normalize( float3(1,1,-2) ), norms[pid])) * float3(1,1,1);

	float3 blended = (1.0 - intensity) * float3(0.9, 0.9, 1) + intensity * light;
	result.color = float4(blended, 1.0f);
	result.uv.xy = uvs[vid] / 17.0f + cbv[index].color.xy;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return g_texture.Sample( g_sampler, input.uv.xy ) * input.color;
}
