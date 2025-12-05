#include "TerrainApp.h"
#include "DDSTextureLoader.h"
#include <sstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const int gNumFrameResources = 1;

TerrainApp::TerrainApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Terrain DX12";
}

TerrainApp::~TerrainApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TerrainApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // Reset command list for initialization
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Initialize camera
    mCamera.SetPosition(1024.0f, 300.0f, 200.0f);
    mCamera.SetProjectionValues(45.0f, AspectRatio(), 1.0f, 10000.0f);

    LoadTextures();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildTerrainGeometry();
    BuildDescriptorHeaps();
    BuildPSO();

    // Execute initialization commands
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait for initialization to complete
    FlushCommandQueue();

    // Create constant buffer
    mPassCB = std::make_unique<UploadBuffer<PassConstants>>(md3dDevice.Get(), 1, true);

    return true;
}

void TerrainApp::OnResize()
{
    D3DApp::OnResize();

    // Update projection matrix
    mCamera.SetProjectionValues(45.0f, AspectRatio(), 1.0f, 10000.0f);
}

void TerrainApp::Update(const GameTimer& gt)
{
    UpdatePassCB(gt);
    UpdateVisibleTiles();
}

void TerrainApp::Draw(const GameTimer& gt)
{
    // Reuse command allocator
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSOs["terrain"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Transition back buffer to render target
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear render target and depth buffer - use dark blue to verify rendering works
    float clearColor[] = { 0.1f, 0.1f, 0.3f, 1.0f };
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Set render targets
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Set descriptor heaps
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // Set root signature
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // Set pass constant buffer
    mCommandList->SetGraphicsRootConstantBufferView(0, mPassCB->Resource()->GetGPUVirtualAddress());

    // Set heightmap texture (slot 1)
    CD3DX12_GPU_DESCRIPTOR_HANDLE heightmapHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    heightmapHandle.Offset(mHeightmapSrvIndex, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, heightmapHandle);

    // Set vertex and index buffers
    mCommandList->IASetVertexBuffers(0, 1, &mTerrainVBV);
    mCommandList->IASetIndexBuffer(&mTerrainIBV);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);

    // Debug: draw all tiles without frustum culling
    static int debugFrame = 0;
    if (debugFrame++ % 120 == 0)
    {
        OutputDebugStringA(("Drawing " + std::to_string(mVisibleTiles.size()) + " tiles, textures: " + std::to_string(mTextures.size()) + "\n").c_str());
        XMFLOAT3 pos = mCamera.GetPosition();
        OutputDebugStringA(("Camera pos: " + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + "\n").c_str());
    }

    // Draw visible tiles
    for (auto* tile : mVisibleTiles)
    {
        // Set color texture for this tile (slot 2)
        // Make sure we don't go out of bounds
        int texIndex = tile->ColorTextureIndex;
        if (texIndex >= 0 && texIndex < (int)mTextures.size())
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE colorHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
            colorHandle.Offset(texIndex, mCbvSrvUavDescriptorSize);
            mCommandList->SetGraphicsRootDescriptorTable(2, colorHandle);
        }

        // Draw tile
        mCommandList->DrawIndexedInstanced(tile->IndexCount, 1, tile->IndexOffset, tile->VertexOffset, 0);
    }

    // Transition back buffer to present
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    // Execute command list
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Present
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}


void TerrainApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void TerrainApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TerrainApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Rotate camera with left mouse button
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update camera rotation
        for (int i = 0; i < abs((int)(dx * 50)); i++)
        {
            if (dx > 0) mCamera.TurnRight();
            else mCamera.TurnLeft();
        }
        for (int i = 0; i < abs((int)(dy * 50)); i++)
        {
            if (dy > 0) mCamera.TurnDown();
            else mCamera.TurnUp();
        }
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void TerrainApp::OnKeyPressed(const GameTimer& gt, WPARAM key)
{
    switch (key)
    {
    case 'W':
        mCamera.MoveForward();
        break;
    case 'S':
        mCamera.MoveBackward();
        break;
    case 'A':
        mCamera.MoveLeft();
        break;
    case 'D':
        mCamera.MoveRight();
        break;
    case 'Q':
        mCamera.TurnLeft();
        break;
    case 'E':
        mCamera.TurnRight();
        break;
    case 'R':
        mCamera.TurnUp();
        break;
    case 'F':
        mCamera.TurnDown();
        break;
    }
}

void TerrainApp::BuildRootSignature()
{
    // Root parameter 0: Pass constants (CBV)
    // Root parameter 1: Heightmap texture (SRV)
    // Root parameter 2: Color texture (SRV)

    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 - heightmap

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1 - color texture

    CD3DX12_ROOT_PARAMETER slotRootParameter[3];
    slotRootParameter[0].InitAsConstantBufferView(0); // b0 - pass constants
    slotRootParameter[1].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_ALL);
    slotRootParameter[2].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

    // Static sampler
    CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 1, &linearClamp,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TerrainApp::BuildShadersAndInputLayout()
{
    OutputDebugStringW(L"Compiling shaders...\n");
    
    mShaders["terrainVS"] = d3dUtil::CompileShader(L"shaders/vs.hlsl", nullptr, "VS", "vs_5_0");
    OutputDebugStringW(L"VS compiled\n");
    
    mShaders["terrainHS"] = d3dUtil::CompileShader(L"shaders/hs.hlsl", nullptr, "HS", "hs_5_0");
    OutputDebugStringW(L"HS compiled\n");
    
    mShaders["terrainDS"] = d3dUtil::CompileShader(L"shaders/ds.hlsl", nullptr, "DS", "ds_5_0");
    OutputDebugStringW(L"DS compiled\n");
    
    mShaders["terrainPS"] = d3dUtil::CompileShader(L"shaders/ps.hlsl", nullptr, "PS", "ps_5_0");
    OutputDebugStringW(L"PS compiled\n");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    
    OutputDebugStringW(L"Shaders and input layout built\n");
}

void TerrainApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { mShaders["terrainVS"]->GetBufferPointer(), mShaders["terrainVS"]->GetBufferSize() };
    psoDesc.HS = { mShaders["terrainHS"]->GetBufferPointer(), mShaders["terrainHS"]->GetBufferSize() };
    psoDesc.DS = { mShaders["terrainDS"]->GetBufferPointer(), mShaders["terrainDS"]->GetBufferSize() };
    psoDesc.PS = { mShaders["terrainPS"]->GetBufferPointer(), mShaders["terrainPS"]->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["terrain"])));
}


void TerrainApp::BuildTerrainGeometry()
{
    std::vector<TerrainVertex> allVertices;
    std::vector<uint16_t> allIndices;

    const float patchSize = (float)TileSize / PatchesPerTile;
    const float totalSizeX = (float)(TilesX * TileSize);
    const float totalSizeZ = (float)(TilesY * TileSize);

    // Create geometry for each tile
    for (int tileY = 0; tileY < TilesY; tileY++)
    {
        for (int tileX = 0; tileX < TilesX; tileX++)
        {
            TerrainTileInfo tile;
            tile.TileX = tileX;
            tile.TileY = tileY;
            tile.TileSize = TileSize;
            tile.VertexOffset = (UINT)allVertices.size();
            tile.IndexOffset = (UINT)allIndices.size();

            float startX = (float)(tileX * TileSize);
            float startZ = (float)(tileY * TileSize);

            // Create vertices for this tile
            for (int z = 0; z <= PatchesPerTile; z++)
            {
                for (int x = 0; x <= PatchesPerTile; x++)
                {
                    TerrainVertex vertex;

                    float worldX = startX + x * patchSize;
                    float worldZ = startZ + z * patchSize;

                    vertex.Pos = XMFLOAT3(worldX, 0.0f, worldZ);

                    // Global texture coordinates for heightmap (0 to 1 across entire terrain)
                    float globalU = worldX / totalSizeX;
                    float globalV = 1.0f - (worldZ / totalSizeZ); // Flip V

                    // Local texture coordinates for color texture (0 to 1 within this tile)
                    float localU = (float)x / PatchesPerTile;
                    float localV = 1.0f - ((float)z / PatchesPerTile); // Flip V

                    vertex.TexC = XMFLOAT2(globalU, globalV);
                    vertex.LocalTexC = XMFLOAT2(localU, localV);

                    allVertices.push_back(vertex);
                }
            }

            // Create indices for 4-control-point patches
            UINT baseVertex = tile.VertexOffset;
            for (int z = 0; z < PatchesPerTile; z++)
            {
                for (int x = 0; x < PatchesPerTile; x++)
                {
                    int topLeft = z * (PatchesPerTile + 1) + x;
                    int topRight = topLeft + 1;
                    int bottomLeft = (z + 1) * (PatchesPerTile + 1) + x;
                    int bottomRight = bottomLeft + 1;

                    allIndices.push_back((uint16_t)(topLeft));
                    allIndices.push_back((uint16_t)(topRight));
                    allIndices.push_back((uint16_t)(bottomLeft));
                    allIndices.push_back((uint16_t)(bottomRight));
                }
            }

            tile.IndexCount = PatchesPerTile * PatchesPerTile * 4;

            // Calculate bounding box
            float minY = 0.0f;
            float maxY = 500.0f; // Match height scale in shader

            XMFLOAT3 minPoint(startX, minY, startZ);
            XMFLOAT3 maxPoint(startX + TileSize, maxY, startZ + TileSize);

            tile.Bounds = BoundingBox(
                XMFLOAT3((minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f, (minPoint.z + maxPoint.z) * 0.5f),
                XMFLOAT3((maxPoint.x - minPoint.x) * 0.5f, (maxPoint.y - minPoint.y) * 0.5f, (maxPoint.z - minPoint.z) * 0.5f)
            );

            // Texture indices - textures are loaded in order: heightmap (0), then color textures (1-16)
            // Color textures are loaded in order y=0..3, x=0..3 with fileY = (TilesY-1) - y
            // So tile at (tileX, tileY) uses texture at index 1 + tileY * TilesX + tileX
            tile.ColorTextureIndex = 1 + tileY * TilesX + tileX;
            tile.NormalTextureIndex = -1; // Not used for now

            OutputDebugStringA(("Tile (" + std::to_string(tileX) + "," + std::to_string(tileY) + ") -> texture index " + std::to_string(tile.ColorTextureIndex) + "\n").c_str());

            mTiles.push_back(tile);
        }
    }

    // Create vertex buffer
    const UINT vbByteSize = (UINT)allVertices.size() * sizeof(TerrainVertex);
    const UINT ibByteSize = (UINT)allIndices.size() * sizeof(uint16_t);

    mTerrainVertexBuffer = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), allVertices.data(), vbByteSize, mTerrainVertexUploadBuffer);

    mTerrainIndexBuffer = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), allIndices.data(), ibByteSize, mTerrainIndexUploadBuffer);

    // Setup vertex buffer view
    mTerrainVBV.BufferLocation = mTerrainVertexBuffer->GetGPUVirtualAddress();
    mTerrainVBV.StrideInBytes = sizeof(TerrainVertex);
    mTerrainVBV.SizeInBytes = vbByteSize;

    // Setup index buffer view
    mTerrainIBV.BufferLocation = mTerrainIndexBuffer->GetGPUVirtualAddress();
    mTerrainIBV.Format = DXGI_FORMAT_R16_UINT;
    mTerrainIBV.SizeInBytes = ibByteSize;

    OutputDebugStringA(("Total vertices: " + std::to_string(allVertices.size()) + "\n").c_str());
    OutputDebugStringA(("Total indices: " + std::to_string(allIndices.size()) + "\n").c_str());
    OutputDebugStringA(("Total tiles: " + std::to_string(mTiles.size()) + "\n").c_str());
}

void TerrainApp::BuildDescriptorHeaps()
{
    // Create SRV heap: 1 heightmap + 16 color textures
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1 + TilesX * TilesY;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // Create SRVs for all textures
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (size_t i = 0; i < mTextures.size(); i++)
    {
        auto& tex = mTextures[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = tex->Resource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = tex->Resource->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
        hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    }
}


void TerrainApp::LoadTextures()
{
    // Load global heightmap from 003 folder
    auto heightmapTex = std::make_unique<Texture>();
    heightmapTex->Name = "heightmap";
    heightmapTex->Filename = L"Terrain/003/Height_Out.dds";

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), heightmapTex->Filename.c_str(),
        heightmapTex->Resource, heightmapTex->UploadHeap));

    mHeightmapSrvIndex = 0;
    mTextures.push_back(std::move(heightmapTex));

    OutputDebugStringW(L"Loaded global heightmap from Terrain/003/Height_Out.dds\n");

    // Load 4x4 grid of color textures from 001 folder
    // Invert Y to match texture file naming with world coordinates
    for (int y = 0; y < TilesY; y++)
    {
        for (int x = 0; x < TilesX; x++)
        {
            // Invert Y for texture file loading to match world space
            int fileY = (TilesY - 1) - y;

            std::wstringstream texSS;
            texSS << L"Terrain/001/Weathering/Weathering_Out_y" << fileY << L"_x" << x << L".dds";
            std::wstring texturePath = texSS.str();

            auto colorTex = std::make_unique<Texture>();
            colorTex->Name = "color_" + std::to_string(y) + "_" + std::to_string(x);
            colorTex->Filename = texturePath;

            HRESULT hr = DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
                mCommandList.Get(), colorTex->Filename.c_str(),
                colorTex->Resource, colorTex->UploadHeap);

            if (SUCCEEDED(hr))
            {
                mTextures.push_back(std::move(colorTex));
                OutputDebugStringW((L"Loaded tile texture: " + texturePath + L"\n").c_str());
            }
            else
            {
                OutputDebugStringW((L"Failed to load tile texture: " + texturePath + L"\n").c_str());
            }
        }
    }

    OutputDebugStringA(("Total textures loaded: " + std::to_string(mTextures.size()) + "\n").c_str());
}

void TerrainApp::UpdatePassCB(const GameTimer& gt)
{
    // Camera matrices are already transposed (row-major) in Camera class
    // They are ready to be used directly in HLSL
    XMMATRIX view = mCamera.GetViewMatrix();
    XMMATRIX proj = mCamera.GetProjectionMatrix();
    
    // For viewProj, we need to multiply the transposed matrices
    // Since they're already transposed: (A^T * B^T) = (B * A)^T
    // So we multiply in reverse order
    XMMATRIX viewProj = XMMatrixMultiply(proj, view);

    PassConstants passConstants;
    XMStoreFloat4x4(&passConstants.View, view);
    XMStoreFloat4x4(&passConstants.Proj, proj);
    XMStoreFloat4x4(&passConstants.ViewProj, viewProj);
    passConstants.EyePosW = mCamera.GetPosition();
    passConstants.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    passConstants.NearZ = 1.0f;
    passConstants.FarZ = 10000.0f;
    passConstants.TotalTime = gt.TotalTime();
    passConstants.DeltaTime = gt.DeltaTime();
    passConstants.HeightScale = 500.0f;

    mPassCB->CopyData(0, passConstants);
}

void TerrainApp::UpdateVisibleTiles()
{
    mVisibleTiles.clear();

    // Temporarily disable frustum culling to debug - show all tiles
    for (auto& tile : mTiles)
    {
        mVisibleTiles.push_back(&tile);
    }

    // Debug output
    static int frameCount = 0;
    if (frameCount++ % 60 == 0)
    {
        OutputDebugStringA(("Visible tiles: " + std::to_string(mVisibleTiles.size()) + "\n").c_str());
    }
}

BoundingFrustum TerrainApp::GetFrustum() const
{
    XMMATRIX proj = mCamera.GetProjectionMatrix();

    // Create frustum from projection matrix
    BoundingFrustum frustum(proj);

    // Transform frustum to world space using inverse view matrix
    XMMATRIX view = mCamera.GetViewMatrix();
    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    frustum.Transform(frustum, invView);

    return frustum;
}
