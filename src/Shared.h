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
		mDevice(device),
		mMappedVoxels(nullptr),
		mTextureData(),
		mTextureUploadHeap()
	{}

	void Init(ID3D12GraphicsCommandList* commandList, std::string& filename);

	ComPtr<ID3D12Resource> mVoxels;
	ComPtr<ID3D12Resource> mTexture;
private:


	template<typename T>
	using deleted_unique_ptr = std::unique_ptr<T, std::function<void(T*)>>;

	ID3D12Device*				mDevice;
	std::vector<Voxel>			mVoxelData;
	Voxel*						mMappedVoxels;
	deleted_unique_ptr<stbi_uc> mTextureData;
	ComPtr<ID3D12Resource>		mTextureUploadHeap;

	void					CreateTexture(ID3D12GraphicsCommandList* commandList, std::string& filename);
	ComPtr<ID3D12Resource>  CreateVoxels();
};