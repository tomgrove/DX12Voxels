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

cbuffer SceneConstantBuffer : register(b0)
{
	float4 velocity;
	float4 offset;
	float4 color;
};

cbuffer ViewConstantBuffer : register(b1)
{
	float4x4 projection;
	float4 tileoffset;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
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

	result.position = mul( verts[vid + pid*4]  + offset + tileoffset, projection);

	float intensity = saturate((16.0f - result.position.z) / 2.0f);
	float3 light = saturate(dot(normalize( float3(1,1,-2) ), norms[pid])) * float3(1,1,1);

	float3 blended = (1.0 - intensity) * float3(0.9, 0.9, 1) + intensity * color.xyz * light;
	result.color = float4(blended, 1.0f);

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}
