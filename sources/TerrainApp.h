#pragma once

#include "d3dApp.h"
#include "d3dUtil.h"
#include "Camera.h"
#include "UploadBuffer.h"
#include "MathHelper.h"
#include "QuadTree.h"
#include <DirectXCollision.h>

// Vertex structure for terrain patches
struct TerrainVertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT2 TexC;      // Global coords for heightmap
    DirectX::XMFLOAT2 LocalTexC; // Local coords for color texture
};

// Constant buffer for matrices
struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float Padding1;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
    float HeightScale = 500.0f;
    float Padding2;
};

// Terrain tile info
struct TerrainTileInfo
{
    int TileX;
    int TileY;
    int TileSize;
    DirectX::BoundingBox Bounds;
    UINT VertexOffset;
    UINT IndexOffset;
    UINT IndexCount;
    int ColorTextureIndex;
    int NormalTextureIndex;
};

class TerrainApp : public D3DApp
{
public:
    TerrainApp(HINSTANCE hInstance);
    TerrainApp(const TerrainApp& rhs) = delete;
    TerrainApp& operator=(const TerrainApp& rhs) = delete;
    ~TerrainApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
    virtual void OnKeyPressed(const GameTimer& gt, WPARAM key) override;

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildPSO();
    void BuildTerrainGeometry();
    void BuildDescriptorHeaps();
    void LoadTextures();
    void UpdatePassCB(const GameTimer& gt);

    // Frustum culling
    void UpdateVisibleTiles();
    DirectX::BoundingFrustum GetFrustum() const;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // Terrain data
    Microsoft::WRL::ComPtr<ID3D12Resource> mTerrainVertexBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTerrainIndexBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTerrainVertexUploadBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTerrainIndexUploadBuffer = nullptr;

    D3D12_VERTEX_BUFFER_VIEW mTerrainVBV;
    D3D12_INDEX_BUFFER_VIEW mTerrainIBV;

    std::vector<TerrainTileInfo> mTiles;
    std::vector<TerrainTileInfo*> mVisibleTiles;
    
    // Quadtree for LOD
    QuadTree mQuadTree;

    // Textures
    std::vector<std::unique_ptr<Texture>> mTextures;
    int mHeightmapSrvIndex = -1;

    // Constant buffers
    std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;

    // Camera
    Camera mCamera;

    // Mouse
    POINT mLastMousePos;

    // Terrain settings
    static const int TilesX = 4;
    static const int TilesY = 4;
    static const int TileSize = 512;
    static const int PatchesPerTile = 16;
    
    // Wireframe mode toggle
    bool mWireframeMode = false;
};
