#include "stdafx.h"
#include "Shared.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void SharedResources::CreateCounterReset()
{
	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mCounterReset)));

	UINT8* pMappedCounterReset = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(mCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset)));
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	mCounterReset->Unmap(0, nullptr);
}

void SharedResources::CreateCommands(ID3D12GraphicsCommandList* commandList)
{
	mCommandsData.resize(BrickResourceCount);
	const UINT commandBufferSize = CommandSizePerTile;

	D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize);
	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&commandBufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mCommands)));

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mCommandsUpload)));

	NAME_D3D12_OBJECT(mCommands);

	for (UINT i = 0; i < BrickCount; i++)
	{
		mCommandsData[i].Data = i;
		mCommandsData[i].DrawArguments.VertexCountPerInstance = 4;
		mCommandsData[i].DrawArguments.InstanceCount = 6 * VoxelsPerBrick;
		mCommandsData[i].DrawArguments.StartVertexLocation = 0;
		mCommandsData[i].DrawArguments.StartInstanceLocation = 0;
	}

	// Copy data to the intermediate upload heap and then schedule a copy
	// from the upload heap to the command buffer.
	D3D12_SUBRESOURCE_DATA commandData = {};
	commandData.pData = reinterpret_cast<UINT8*>(&mCommandsData[0]);
	commandData.RowPitch = commandBufferSize;
	commandData.SlicePitch = commandData.RowPitch;

	UpdateSubresources<1>(commandList, mCommands.Get(), mCommandsUpload.Get(), 0, 0, 1, &commandData);
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( mCommands.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}

ComPtr<ID3D12Resource>  SharedResources::CreateVoxels()
{
	ComPtr<ID3D12Resource> Buffer;

	const UINT constantBufferDataSize = VoxelCount * FrameCount * sizeof(Voxel);

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(constantBufferDataSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&Buffer)));

		NAME_D3D12_OBJECT(Buffer);

	for (int bz = 0; bz < cDepthInBricks; bz++)
	{
		for (int by = 0; by < cHeightInBricks; by++)
		{
			for (int bx = 0; bx < cWidthInBricks; bx++)
			{
				for (int vz = 0; vz < cBrickDepth; vz++)
				{
					for (int vy = 0; vy < cBrickHeight; vy++)
					{
						for (int vx = 0; vx < cBrickWidth; vx++)
						{
							UINT brick = bz * (cWidthInBricks*cHeightInBricks) + by * cWidthInBricks + bx;
							UINT voxel = vz * (cBrickWidth*cBrickHeight) + vy * cBrickWidth + vx;
							UINT n = brick * VoxelsPerBrick + voxel;

							UINT x = bx * cBrickWidth + vx;
							UINT y = by * cBrickHeight + vy;
							UINT z = bz * cBrickDepth + vz;

							auto v0 = (cos((float)x / Width * 3.141f * 4.0f + 1.0f));
							auto v1 = (sin((float)z / Depth * 3.141f * 4.0f + 1.0f));

							auto v3 = (v0*v1) / 2.0f + 0.5f;
							auto surface = v3*(Height / 1 - 1);
							if (y < surface  && y >(surface - 6))
							{
								int tx = 13;  //rand() % 16;
								int ty = 4 * 16; // rand() % 16;
								tx += 6 * 256;
								ty += 3 * (256 * 16);
								mVoxelData[n].mMaterial = rand() % 65536; // tx + ty; //XMFLOAT2((float)tx / 16.0f, (float)ty / 16.0f);// , GetRandomFloat(0.0f, 1.0f), 1.0f);
							}
							else if (y < (surface - 2))
							{
								int tx = 2;  //rand() % 16;
								int ty = 0; // rand() % 16;
								mVoxelData[n].mMaterial = rand() % 65536; // tx + ty * 16; // XMFLOAT2((float)tx / 16.0f, (float)ty / 16.0f); // , GetRandomFloat(0.0f, 1.0f), 1.0f);
							}
							else
							{
								mVoxelData[n].mMaterial = 0; // XMFLOAT2(0, 0);
							}
						}
					}
				}
			}
		}
	}

	{
		CD3DX12_RANGE readRange(0, 0);
		ThrowIfFailed(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&mMappedVoxels)));
		const size_t size = VoxelCount * sizeof(Voxel);
		memcpy(mMappedVoxels, &mVoxelData[0], size);
	}

	return Buffer;
}

void SharedResources::CreateTexture(ID3D12GraphicsCommandList* commandList, std::string& filename)
{
	int w, h, n;
	mTextureData = deleted_unique_ptr<stbi_uc>(stbi_load(filename.c_str(), &w, &h, &n, 0), [](stbi_uc* texture) {
		stbi_image_free(texture);
	});

	// Describe and create a Texture2D.
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = w;
	textureDesc.Height = h;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mTexture)));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mTexture.Get(), 0, 1);

	// Create the GPU upload buffer.
	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mTextureUpload)));

	// Copy data to the intermediate upload heap and then schedule a copy 
	// from the upload heap to the Texture2D.

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = mTextureData.get();
	textureData.RowPitch = w * n;
	textureData.SlicePitch = textureData.RowPitch * n;

	UpdateSubresources( commandList, mTexture.Get(), mTextureUpload.Get(), 0, 0, 1, &textureData);
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}


void SharedResources::Init(ID3D12GraphicsCommandList* commandList, std::string& filename )
{
	mVoxels = CreateVoxels();
	CreateTexture( commandList, filename );
	CreateCommands( commandList );
	CreateCounterReset();
}
