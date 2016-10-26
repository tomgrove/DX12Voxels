#pragma once

#include <string>
#include <memory>
#include <functional>
#include "Definitions.h"
#include "stb_image.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class SharedResources
{
public:
	SharedResources(ID3D12Device* device) :
		mVoxels(),
		mTexture(),
		mCommands(),
		mCounterReset(),
		mDevice(device),
		mTextureData(),
		mTextureUpload(),
		mCommandsUpload(),
		mMappedVoxels(nullptr)
	{}

	void Init(ID3D12GraphicsCommandList* commandList, std::string& filename);

	ComPtr<ID3D12Resource> mVoxels;
	ComPtr<ID3D12Resource> mTexture;
	ComPtr<ID3D12Resource> mCommands;
	ComPtr<ID3D12Resource> mCounterReset;

private:

	template<typename T>
	using deleted_unique_ptr = std::unique_ptr<T, std::function<void(T*)>>;

	ID3D12Device*					mDevice;

	std::vector<Voxel>				mVoxelData;
	std::vector<DrawVoxelCommand>	mCommandsData;
	deleted_unique_ptr<stbi_uc>		mTextureData;

	ComPtr<ID3D12Resource>			mTextureUpload;
	ComPtr<ID3D12Resource>			mCommandsUpload;

	Voxel*							mMappedVoxels;

	void							CreateCounterReset();
	void							CreateCommands(ID3D12GraphicsCommandList* commandList);
	void							CreateTexture(ID3D12GraphicsCommandList* commandList, std::string& filename);
	ComPtr<ID3D12Resource>			CreateVoxels();
};