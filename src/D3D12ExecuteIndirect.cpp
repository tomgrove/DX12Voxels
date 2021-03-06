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

#include "stdafx.h"
#include "D3D12ExecuteIndirect.h"
#include "stb_image.h"

const UINT D3D12ExecuteIndirect::CommandSizePerFrame = BrickCount * sizeof(IndirectCommand);
const UINT D3D12ExecuteIndirect::CommandBufferCounterOffset = AlignForUavCounter(D3D12ExecuteIndirect::CommandSizePerFrame);
const float D3D12ExecuteIndirect::VoxelHalfWidth = cVoxelHalfWidth;

D3D12ExecuteIndirect::D3D12ExecuteIndirect(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_bufIndex( 0 ),
	m_RunCompute( true ),
	m_viewport(),
	m_scissorRect(),
	m_cullingScissorRect(),
	m_rtvDescriptorSize(0),
	m_cbvSrvUavDescriptorSize(0),
	m_csRootConstants(),
	m_Yaw(0)
{
	ZeroMemory(m_fenceValues, sizeof(m_fenceValues));
	m_constantBufferData.resize(VoxelCount);

	m_csRootConstants.commandCount = BrickCount;

	m_viewport.Width = static_cast<float>(width);
	m_viewport.Height = static_cast<float>(height);
	m_viewport.MaxDepth = 1.0f;

	m_scissorRect.right = static_cast<LONG>(width);
	m_scissorRect.bottom = static_cast<LONG>(height);

	m_Position = XMFLOAT3(-0.1f * cWidth/2 , -0.1f * cHeight/2, -0.1f * cDepth/2);
	m_VoxOp = None;
}

void D3D12ExecuteIndirect::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12ExecuteIndirect::LoadPipeline()
{
#if defined(_DEBUG)
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
			));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
			));
	}

	// Describe and create the command queues.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
	NAME_D3D12_OBJECT(m_commandQueue);

	D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
	computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

	ThrowIfFailed(m_device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&m_computeCommandQueue)));
	NAME_D3D12_OBJECT(m_computeCommandQueue);

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
		));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

		// Describe and create a constant buffer view (CBV), Shader resource
		// view (SRV), and unordered access view (UAV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
		cbvSrvUavHeapDesc.NumDescriptors = CbvSrvUavDescriptorCountPerFrame * FrameCount + NumTexture;
		cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap)));
		NAME_D3D12_OBJECT(m_cbvSrvUavHeap);

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV and command allocators for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);

			NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);

			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeCommandAllocators[n])));
			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_cullCommandAllocators[n])));
		}
	}
}

static float MD(XMVECTOR pt, XMVECTOR tl, XMVECTOR br, float hs[4] )
{
}

// Load the sample assets.
void D3D12ExecuteIndirect::LoadAssets()
{
	unsigned char* texturedata = nullptr;
	// Create the root signatures.
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount];

		CD3DX12_DESCRIPTOR_RANGE1 texranges[1];
		texranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		rootParameters[Texture].InitAsDescriptorTable(1, &texranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_DESCRIPTOR_RANGE1 voxelranges[1];
		voxelranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		rootParameters[Cbv].InitAsDescriptorTable(1, &voxelranges[0], D3D12_SHADER_VISIBILITY_VERTEX); //D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
		
		rootParameters[View].InitAsConstants(ViewInUInt32s, 1); 

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
		NAME_D3D12_OBJECT(m_rootSignature);

		// Create compute signature.
		{
			CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

			CD3DX12_ROOT_PARAMETER1 computeRootParameters[ComputeRootParametersCount];
			computeRootParameters[SrvUavTable].InitAsDescriptorTable(2, ranges);
			computeRootParameters[RootConstants].InitAsConstants(4, 0);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
			computeRootSignatureDesc.Init_1_1(_countof(computeRootParameters), computeRootParameters);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature)));
			NAME_D3D12_OBJECT(m_computeRootSignature);
		}
		// Create cull signature
		{
			CD3DX12_ROOT_PARAMETER1 cullRootParameters[CullRootParametersCount];

			CD3DX12_DESCRIPTOR_RANGE1 srvranges[1];
			srvranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			cullRootParameters[SrvTable].InitAsDescriptorTable(1, srvranges);

			CD3DX12_DESCRIPTOR_RANGE1 uavranges[1];
			uavranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			cullRootParameters[UavTable].InitAsDescriptorTable(1, uavranges);

			cullRootParameters[CullRootConstants].InitAsConstants(CullConstantsInU32, 0);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC cullRootSignatureDesc;
			cullRootSignatureDesc.Init_1_1(_countof(cullRootParameters), cullRootParameters);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&cullRootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_cullRootSignature)));
			NAME_D3D12_OBJECT(m_cullRootSignature);
		}
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;
		ComPtr<ID3DBlob> computeShader;
		ComPtr<ID3DBlob> cullShader;
		ComPtr<ID3DBlob> error;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		HRESULT hr = D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
		if (FAILED(hr))
		{
			OutputDebugStringA((char*)error->GetBufferPointer());
			throw std::exception();
		}
		hr = D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
		if (FAILED(hr))
		{
			OutputDebugStringA((char*)error->GetBufferPointer());
			throw std::exception();
		}
		hr = D3DCompileFromFile(GetAssetFullPath(L"compute.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSMain", "cs_5_0", compileFlags, 0, &computeShader, &error);
		if (FAILED(hr))
		{
			OutputDebugStringA((char*)error->GetBufferPointer());
			throw std::exception();
		}
		hr = D3DCompileFromFile(GetAssetFullPath(L"cull.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSMain", "cs_5_0", compileFlags, 0, &cullShader, &error);
		if (FAILED(hr))
		{
			OutputDebugStringA((char*)error->GetBufferPointer());
			throw std::exception();
		}


		// Describe and create the graphics pipeline state objects (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
		NAME_D3D12_OBJECT(m_pipelineState);

		// Describe and create the compute pipeline state object (PSO).
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
			computePsoDesc.pRootSignature = m_computeRootSignature.Get();
			computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

			ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computeState)));
			NAME_D3D12_OBJECT(m_computeState);
		}

		// Describe and create the compute pipeline state object (PSO).
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC cullPsoDesc = {};
			cullPsoDesc.pRootSignature = m_cullRootSignature.Get();
			cullPsoDesc.CS = CD3DX12_SHADER_BYTECODE(cullShader.Get());

			ThrowIfFailed(m_device->CreateComputePipelineState(&cullPsoDesc, IID_PPV_ARGS(&m_cullState)));
			NAME_D3D12_OBJECT(m_cullState);
		}
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_bufIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocators[m_bufIndex].Get(), m_computeState.Get(), IID_PPV_ARGS(&m_computeCommandList)));
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_cullCommandAllocators[m_bufIndex].Get(), m_cullState.Get(), IID_PPV_ARGS(&m_cullCommandList)));
	ThrowIfFailed(m_computeCommandList->Close());
	ThrowIfFailed(m_cullCommandList->Close());

	NAME_D3D12_OBJECT(m_commandList);
	NAME_D3D12_OBJECT(m_computeCommandList);
	NAME_D3D12_OBJECT(m_cullCommandList);

	// Note: ComPtr's are CPU objects but these resources need to stay in scope until
	// the command list that references them has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resources are not
	// prematurely destroyed.

	ComPtr<ID3D12Resource> commandBufferUpload;

	// Create the depth stencil view.
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
		depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthStencil)
			));

		NAME_D3D12_OBJECT(m_depthStencil);

		m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}
	ComPtr<ID3D12Resource> textureUploadHeap;

	// Create the texture.
	{
		int w, h, n;
		texturedata = stbi_load("mc.png", &w, &h, &n, 0);

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

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_texture)));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

		// Create the GPU upload buffer.
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the Texture2D.


		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = texturedata;
		textureData.RowPitch = w * n;
		textureData.SlicePitch = textureData.RowPitch * n;

		UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_cbvSrvUavDescriptorSize);

		m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, cbvSrvHandle); 
	}

	// Create the constant buffers.
	{

		const UINT constantBufferDataSize = VoxelCount * FrameCount * sizeof(SceneConstantBuffer);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(constantBufferDataSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer)));

		NAME_D3D12_OBJECT(m_constantBuffer);

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
								if (y < surface  && y > (surface-6) )
								{
									int tx = 13;  //rand() % 16;
									int ty = 4 * 16; // rand() % 16;
									tx += 6 * 256;
									ty += 3 * (256 * 16);
									m_constantBufferData[n].color = rand() % 65536; // tx + ty; //XMFLOAT2((float)tx / 16.0f, (float)ty / 16.0f);// , GetRandomFloat(0.0f, 1.0f), 1.0f);
								}
								else if (y < (surface- 2))
								{
									int tx = 2;  //rand() % 16;
									int ty = 0; // rand() % 16;
									m_constantBufferData[n].color = rand() % 65536; // tx + ty * 16; // XMFLOAT2((float)tx / 16.0f, (float)ty / 16.0f); // , GetRandomFloat(0.0f, 1.0f), 1.0f);
								} 
								else
								{
									m_constantBufferData[n].color = 0; // XMFLOAT2(0, 0);
								}
							}
						}
					}
				}
			}
		}

		{
			CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
			ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
			const size_t size = VoxelCount * sizeof(SceneConstantBuffer);
			memcpy(m_pCbvDataBegin, &m_constantBufferData[0], size);
		}

		// Create shader resource views (SRV) of the constant buffers for the
		// compute shader to read from.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = VoxelCount;
		srvDesc.Buffer.StructureByteStride = sizeof(SceneConstantBuffer);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.FirstElement = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CbvSrvOffset + NumTexture, m_cbvSrvUavDescriptorSize);
		for (int i = 0; i < FrameCount; i++)
		{
			srvDesc.Buffer.FirstElement = i * VoxelCount;
			m_device->CreateShaderResourceView(m_constantBuffer.Get(), &srvDesc, cbvSrvHandle);
			cbvSrvHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
		}
	}

	// Create the command signature used for indirect drawing.
	{
		// Each command consists of a CBV update and a DrawInstanced call.
		D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
		argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
		argumentDescs[0].Constant.DestOffsetIn32BitValues = offsetof(ViewConstantBuffer,index) / sizeof(UINT);
		argumentDescs[0].Constant.RootParameterIndex = View;
		argumentDescs[0].Constant.Num32BitValuesToSet = 1;

		argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

		D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
		commandSignatureDesc.pArgumentDescs = argumentDescs;
		commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
		commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

		ThrowIfFailed(m_device->CreateCommandSignature(&commandSignatureDesc, m_rootSignature.Get(), IID_PPV_ARGS(&m_commandSignature)));
		NAME_D3D12_OBJECT(m_commandSignature);
	}

	// Create the command buffers and UAVs to store the results of the compute work.
	{
		std::vector<IndirectCommand> commands;
		commands.resize(BrickResourceCount);
		const UINT commandBufferSize = CommandSizePerFrame ;

		D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize);
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&commandBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_commandBuffer)));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&commandBufferUpload)));

		NAME_D3D12_OBJECT(m_commandBuffer);

		UINT commandIndex = 0;

		for (UINT n = 0; n < BrickCount; n++)
		{
			commands[commandIndex].index = n;
			commands[commandIndex].drawArguments.VertexCountPerInstance = 4;
			commands[commandIndex].drawArguments.InstanceCount = 6 * VoxelsPerBrick;
			commands[commandIndex].drawArguments.StartVertexLocation = 0;
			commands[commandIndex].drawArguments.StartInstanceLocation = 0;

			commandIndex++;
		}

		// Copy data to the intermediate upload heap and then schedule a copy
		// from the upload heap to the command buffer.
		D3D12_SUBRESOURCE_DATA commandData = {};
		commandData.pData = reinterpret_cast<UINT8*>(&commands[0]);
		commandData.RowPitch = commandBufferSize;
		commandData.SlicePitch = commandData.RowPitch;

		UpdateSubresources<1>(m_commandList.Get(), m_commandBuffer.Get(), commandBufferUpload.Get(), 0, 0, 1, &commandData);
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

		// Create SRVs for the command buffers.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = BrickCount;
		srvDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.FirstElement = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CommandsOffset + NumTexture , m_cbvSrvUavDescriptorSize);
		for (int i = 0; i < FrameCount; i++)
		{
			m_device->CreateShaderResourceView(m_commandBuffer.Get(), &srvDesc, commandsHandle);
			commandsHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
		}

		{
			// Create the unordered access views (UAVs) that store the results of the compute work.
			CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), ProcessedCommandsOffset + NumTexture, m_cbvSrvUavDescriptorSize);
			for (UINT frame = 0; frame < FrameCount; frame++)
			{
				// Allocate a buffer large enough to hold all of the indirect commands
				// for a single frame as well as a UAV counter.
				commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(CommandBufferCounterOffset + sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
				ThrowIfFailed(m_device->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
					D3D12_HEAP_FLAG_NONE,
					&commandBufferDesc,
					D3D12_RESOURCE_STATE_COPY_DEST,
					nullptr,
					IID_PPV_ARGS(&m_processedCommandBuffers[frame])));

				NAME_D3D12_OBJECT_INDEXED(m_processedCommandBuffers, frame);

				D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.Format = DXGI_FORMAT_UNKNOWN;
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uavDesc.Buffer.FirstElement = 0;
				uavDesc.Buffer.NumElements = BrickCount;
				uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
				uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
				uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

				m_device->CreateUnorderedAccessView(
					m_processedCommandBuffers[frame].Get(),
					m_processedCommandBuffers[frame].Get(),
					&uavDesc,
					processedCommandsHandle);

				processedCommandsHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
			}

			CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsCountHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), ProcessedCommandsCountOffset + NumTexture, m_cbvSrvUavDescriptorSize);
			for (UINT frame = 0; frame < FrameCount; frame++)
			{
				// Create SRVs for the command buffers.
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Buffer.NumElements = 1;
				srvDesc.Buffer.StructureByteStride = sizeof(UINT);
				srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				srvDesc.Buffer.FirstElement = CommandBufferCounterOffset / sizeof(UINT); 

				m_device->CreateShaderResourceView(m_processedCommandBuffers[frame].Get(), &srvDesc, processedCommandsCountHandle);
				processedCommandsCountHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
			}

			// Allocate a buffer that can be used to reset the UAV counters and initialize
			// it to 0.
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_processedCommandBufferCounterReset)));

			UINT8* pMappedCounterReset = nullptr;
			CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
			ThrowIfFailed(m_processedCommandBufferCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset)));
			ZeroMemory(pMappedCounterReset, sizeof(UINT));
			m_processedCommandBufferCounterReset->Unmap(0, nullptr);
		}

		{
			// Create the unordered access views (UAVs) that store the culled commands
			CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CullCommandsOffset + NumTexture, m_cbvSrvUavDescriptorSize);
			for (UINT frame = 0; frame < FrameCount; frame++)
			{
				// Allocate a buffer large enough to hold all of the indirect commands
				// for a single frame as well as a UAV counter.
				commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(CommandBufferCounterOffset + sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
				ThrowIfFailed(m_device->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
					D3D12_HEAP_FLAG_NONE,
					&commandBufferDesc,
					D3D12_RESOURCE_STATE_COPY_DEST,
					nullptr,
					IID_PPV_ARGS(&m_cullCommandBuffers[frame])));

				NAME_D3D12_OBJECT_INDEXED(m_cullCommandBuffers, frame);

				D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.Format = DXGI_FORMAT_UNKNOWN;
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uavDesc.Buffer.FirstElement = 0;
				uavDesc.Buffer.NumElements = BrickCount;
				uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
				uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
				uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

				m_device->CreateUnorderedAccessView(
					m_cullCommandBuffers[frame].Get(),
					m_cullCommandBuffers[frame].Get(),
					&uavDesc,
					processedCommandsHandle);

				processedCommandsHandle.Offset(CbvSrvUavDescriptorCountPerFrame, m_cbvSrvUavDescriptorSize);
			}
		}
	}

	// Close the command list and execute it to begin the vertex buffer copy into
	// the default heap.
	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence)));
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

	stbi_image_free(texturedata);
}

// Get a random float value between min and max.
float D3D12ExecuteIndirect::GetRandomFloat(float min, float max)
{
	float scale = static_cast<float>(rand()) / RAND_MAX;
	float range = max - min;
	return scale * range + min;
}

XMFLOAT3 D3D12ExecuteIndirect::GetBrickPositionFromIndex(UINT index) const 
{
	const int slice = cWidthInBricks*cHeightInBricks;
	int bi = index / VoxelsPerBrick;
	float z = static_cast<float>( bi / slice);
	float y = static_cast<float>( (bi % slice) / cWidthInBricks);
	float x = static_cast<float>( (bi % slice) % cWidthInBricks);

	return XMFLOAT3(x, y, z);
}

XMFLOAT3 D3D12ExecuteIndirect::GetVoxelPositionFromIndex(UINT index) const
{
	int vi = index % VoxelsPerBrick;
	const int slice = cBrickWidth*cBrickHeight;
	float z = static_cast<float>( vi / slice);
	float y = static_cast<float>( (vi % slice) / cBrickWidth);
	float x = static_cast<float>( (vi % slice) % cBrickWidth);

	return XMFLOAT3(x, y, z);
}

 XMFLOAT3 D3D12ExecuteIndirect::GetPositionFromIndex(UINT index) const 
{
	 const float Scale = VoxelHalfWidth * 2.0f;
	 auto brick = GetBrickPositionFromIndex(index);
	 auto voxel = GetVoxelPositionFromIndex(index);

	 return  XMFLOAT3( (brick.x * cBrickWidth + voxel.x) * Scale, 
					   (brick.y * cBrickHeight + voxel.y )* Scale, 
					   (brick.z * cBrickDepth + voxel.z) * Scale );
}

// Update frame-based values.
void D3D12ExecuteIndirect::OnUpdate()
{
	auto Proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspectRatio, 0.01f, cDepth * 0.1f);
	auto Rot = XMMatrixRotationRollPitchYaw(0, m_Yaw, 0);
	auto Trans = XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
	auto ViewPos = XMMatrixMultiply(Trans, Rot);
	auto ViewProj = XMMatrixMultiply(ViewPos, Proj);
	XMStoreFloat4x4(&m_View.projection, XMMatrixTranspose(ViewProj));

	if (m_VoxOp != None)
	{
		for (UINT n = 0; n < VoxelCount; n++)
		{
			XMFLOAT3 voxpos = GetPositionFromIndex(n);
			XMFLOAT3 dist(voxpos.x + m_Position.x,
				          voxpos.y + m_Position.y,
				          voxpos.z + (m_Position.z - 2*VoxelHalfWidth));

			if ((dist.x*dist.x + dist.y*dist.y + dist.z*dist.z) < 0.5f ) 
			{
				if (m_VoxOp == Mine)
				{
					m_constantBufferData[n].color = 0; 
				}
				else
				{
					m_constantBufferData[n].color = 7; 
				}
			}
		}

		m_bufIndex =  (m_bufIndex + 1) % FrameCount;
		UINT8* destination = m_pCbvDataBegin + (VoxelCount * m_bufIndex * sizeof(SceneConstantBuffer));
		memcpy(destination, &m_constantBufferData[0], VoxelCount * sizeof(SceneConstantBuffer));
		m_RunCompute = true;
		m_VoxOp = None;
	}
}

// Render the scene.
void D3D12ExecuteIndirect::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandLists();

	{
		UINT computeCmds = 0;
		ID3D12CommandList* ppCommandLists[2];

		// Execute the compute work.

		
		if (m_RunCompute)
		{
			ppCommandLists[computeCmds] = m_computeCommandList.Get();
			computeCmds++;
		}

		ppCommandLists[computeCmds] = m_cullCommandList.Get();
		computeCmds++;

		PIXBeginEvent(m_commandQueue.Get(), 0, L"Compute");
		m_computeCommandQueue->ExecuteCommandLists(computeCmds, ppCommandLists);
		m_computeCommandQueue->Signal(m_computeFence.Get(), m_fenceValues[m_frameIndex]);

		// Execute the rendering work only when the compute work is complete.
		m_commandQueue->Wait(m_computeFence.Get(), m_fenceValues[m_frameIndex]);
		PIXEndEvent(m_commandQueue.Get());
	}

	{
		PIXBeginEvent(m_commandQueue.Get(), 0, L"Render");

		// Execute the rendering work.
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		PIXEndEvent(m_commandQueue.Get());
	}

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(0, 0));

	MoveToNextFrame();
}

void D3D12ExecuteIndirect::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);
}

void D3D12ExecuteIndirect::OnKeyDown(UINT8 key)
{
	const float delta = 0.05f;
	switch (key)
	{
		case VK_UP:
			m_Position.z -= delta * cos(m_Yaw);
			m_Position.x -= delta * -sin(m_Yaw);
			break;
		case VK_DOWN:
			m_Position.z += delta * cos(m_Yaw);
			m_Position.x += delta * -sin(m_Yaw);
			break;
		case VK_LEFT:
			m_Yaw += 0.04f;
			break;
		case VK_RIGHT:
			m_Yaw -= 0.04f;
			break;
		case 'W':
			m_Position.y -= delta;
			break;
		case 'S':
			m_Position.y += delta;
			break;
		case VK_SPACE:
			m_VoxOp = Mine;
			break;
		case VK_INSERT:
			m_VoxOp = Place;
			break;
	}
}

// Fill the command list with all the render commands and dependent state.
void D3D12ExecuteIndirect::PopulateCommandLists()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_computeCommandAllocators[m_frameIndex]->Reset());
	ThrowIfFailed(m_cullCommandAllocators[m_frameIndex]->Reset());
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_computeCommandList->Reset(m_computeCommandAllocators[m_bufIndex].Get(), m_computeState.Get()));
	ThrowIfFailed(m_cullCommandList->Reset(m_cullCommandAllocators[m_frameIndex].Get(), m_cullState.Get()));
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()));

	// Record the compute commands that will cull triangles and prevent them from being processed by the vertex shader.
	if ( m_RunCompute )
	{
		UINT frameDescriptorOffset = m_bufIndex * CbvSrvUavDescriptorCountPerFrame;
		D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

		m_computeCommandList->SetComputeRootSignature(m_computeRootSignature.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
		m_computeCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		m_computeCommandList->SetComputeRootDescriptorTable(
			SrvUavTable,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, CbvSrvOffset + NumTexture + frameDescriptorOffset, m_cbvSrvUavDescriptorSize));

		//m_csRootConstants.cullOffset = m_Position.z;
		m_computeCommandList->SetComputeRoot32BitConstants(RootConstants, ComputeInUInt32s, reinterpret_cast<void*>(&m_csRootConstants), 0);

		// Reset the UAV counter for this frame.
		m_computeCommandList->CopyBufferRegion(m_processedCommandBuffers[m_bufIndex].Get(), CommandBufferCounterOffset, m_processedCommandBufferCounterReset.Get(), 0, sizeof(UINT));

		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffers[m_bufIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_computeCommandList->ResourceBarrier(1, &barrier);

		m_computeCommandList->Dispatch(static_cast<UINT>(ceil(BrickCount / float(ComputeThreadBlockSize))), 1, 1);
	}

	ThrowIfFailed(m_computeCommandList->Close());
	
	{
		UINT uavFrameDescriptorOffset = m_frameIndex * CbvSrvUavDescriptorCountPerFrame;
		UINT srvFrameDescriptorOffset = m_bufIndex * CbvSrvUavDescriptorCountPerFrame;

		D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

		m_cullCommandList->SetComputeRootSignature(m_cullRootSignature.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
		m_cullCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		m_cullCommandList->SetComputeRootDescriptorTable(
			SrvTable,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, ProcessedCommandsOffset + NumTexture + srvFrameDescriptorOffset, m_cbvSrvUavDescriptorSize));
		
		m_cullCommandList->SetComputeRootDescriptorTable(
			UavTable,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, CullCommandsOffset + NumTexture + uavFrameDescriptorOffset, m_cbvSrvUavDescriptorSize));

		memcpy( &m_cullConstants.projection, m_View.projection.m, sizeof( float[4][4] ) );
		m_cullCommandList->SetComputeRoot32BitConstants(CullRootConstants, CullConstantsInU32, reinterpret_cast<void*>(&m_cullConstants), 0);

		// Reset the UAV counter for this frame.
		m_cullCommandList->CopyBufferRegion(m_cullCommandBuffers[m_frameIndex].Get(), CommandBufferCounterOffset, m_processedCommandBufferCounterReset.Get(), 0, sizeof(UINT));

		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cullCommandBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_cullCommandList->ResourceBarrier(1, &barrier);

		m_cullCommandList->Dispatch(static_cast<UINT>(ceil(BrickCount / float(ComputeThreadBlockSize))), 1, 1);
		ThrowIfFailed(m_cullCommandList->Close());
	}

	// Record the rendering commands.
	{
		// Set necessary state.
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
		m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		m_commandList->SetGraphicsRoot32BitConstants(View, ViewInUInt32s, reinterpret_cast<void*>(&m_View), 0);
		m_commandList->SetGraphicsRootDescriptorTable(Texture, m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart() );

		UINT frameDescriptorOffset = m_bufIndex * CbvSrvUavDescriptorCountPerFrame;
		D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvdt(cbvSrvUavHandle, CbvSrvOffset + NumTexture + frameDescriptorOffset, m_cbvSrvUavDescriptorSize);

		m_commandList->SetGraphicsRootDescriptorTable(Cbv,  cbvdt );

		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		// Indicate that the command buffer will be used for indirect drawing
		// and that the back buffer will be used as a render target.

		D3D12_RESOURCE_BARRIER barriers[3];
		UINT barrierIndex = 0;
		if (m_RunCompute)
		{
			barriers[barrierIndex] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_processedCommandBuffers[m_bufIndex].Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_GENERIC_READ);
			barrierIndex++;
		}   

		barriers[barrierIndex] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_cullCommandBuffers[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		barrierIndex++;

		barriers[barrierIndex] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

		barrierIndex++;

		m_commandList->ResourceBarrier(barrierIndex, barriers);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		// Record commands.
		const float clearColor[] = { 0.9f, 0.9f, 1.0f, 1.0f };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		{
			PIXBeginEvent(m_commandList.Get(), 0, L"Draw visible voxels");
			for (int i = 0; i < TileX; i++) {

				for (int j = 0; j < TileZ; j++) {

					m_View.tileoffset = XMFLOAT4( i*VoxelHalfWidth*2.0f*Width, 0, j*VoxelHalfWidth*2.0f*Depth, 0);
					m_commandList->SetGraphicsRoot32BitConstants(View, ViewInUInt32s, reinterpret_cast<void*>(&m_View), 0);
					
					// Hmmm ... so what does ExecuteIndirect use this TriangleCount for ? I assume it has to allocate / reserve
					// some buffer internally; making it smaller improves perf so might there be some "ideal" vendor specific
					// command length?
					// http://developer.download.nvidia.com/gameworks/events/GDC2016/AdvancedRenderingwithDirectX11andDirectX12.pdf
					// strongly recommend Making MaxCommandCount close to actual count, which implies there is some overhead prop
					// to max command count on nvidia.

					m_commandList->ExecuteIndirect(
						m_commandSignature.Get(),
						BrickCount, // er ... make it a bit smaller? and crashier?
						m_cullCommandBuffers[m_frameIndex].Get(),
						0,
						m_cullCommandBuffers[m_frameIndex].Get(),
						CommandBufferCounterOffset);
				}
			}
		}

		PIXEndEvent(m_commandList.Get());

		// Indicate that the command buffer may be used by the compute shader
		// and that the back buffer will now be used to present.

		barrierIndex = 0;

		if (m_RunCompute)
		{
			barriers[barrierIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ; // D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
			barriers[barrierIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrierIndex++;
		}

		barriers[barrierIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		barriers[barrierIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		barrierIndex++;

		barriers[barrierIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barriers[barrierIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		barrierIndex++;

		m_commandList->ResourceBarrier( barrierIndex, barriers);

		ThrowIfFailed(m_commandList->Close());
	}
}

// Wait for pending GPU work to complete.
void D3D12ExecuteIndirect::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12ExecuteIndirect::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
	
	m_RunCompute = false;
}
