#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <vector>
#include <memory>
#include <cstdint>

// LOD уровни для Quadtree
enum class LODLevel
{
    LOD0 = 0,  // Максимальная детализация (близко)
    LOD1 = 1,
    LOD2 = 2,
    LOD3 = 3   // Минимальная детализация (далеко)
};

// Узел Quadtree
struct QuadTreeNode
{
    DirectX::BoundingBox Bounds;           // Границы узла
    DirectX::XMFLOAT3 Center;              // Центр узла
    float Size;                            // Размер узла
    int Depth;                             // Глубина в дереве (0 = корень)
    LODLevel CurrentLOD;                   // Текущий LOD уровень
    bool IsLeaf;                           // Является ли листом
    
    // Дочерние узлы (NW, NE, SW, SE)
    std::unique_ptr<QuadTreeNode> Children[4];
    
    // Индексы для рендеринга
    uint32_t VertexOffset;
    uint32_t IndexOffset;
    uint32_t IndexCount;
    
    // Текстурные координаты для этого узла
    DirectX::XMFLOAT2 TexCoordMin;
    DirectX::XMFLOAT2 TexCoordMax;
    
    QuadTreeNode() : Size(0), Depth(0), CurrentLOD(LODLevel::LOD0), 
                     IsLeaf(true), VertexOffset(0), IndexOffset(0), IndexCount(0) {}
};

// Результат обхода Quadtree - узлы для рендеринга
struct QuadTreeRenderNode
{
    QuadTreeNode* Node;
    LODLevel LOD;
    float DistanceToCamera;
};

class QuadTree
{
public:
    QuadTree();
    ~QuadTree();
    
    // Инициализация дерева
    void Initialize(float terrainSize, int maxDepth, 
                    const std::vector<float>& lodDistances);
    
    // Обновление LOD на основе позиции камеры
    void Update(const DirectX::XMFLOAT3& cameraPos, 
                const DirectX::BoundingFrustum& frustum);
    
    // Получить узлы для рендеринга
    const std::vector<QuadTreeRenderNode>& GetVisibleNodes() const { return mVisibleNodes; }
    
    // Получить корневой узел
    QuadTreeNode* GetRoot() { return mRoot.get(); }
    
    // Параметры
    int GetMaxDepth() const { return mMaxDepth; }
    float GetTerrainSize() const { return mTerrainSize; }

private:
    void BuildTree(QuadTreeNode* node, float x, float z, float size, int depth);
    void UpdateNode(QuadTreeNode* node, const DirectX::XMFLOAT3& cameraPos,
                    const DirectX::BoundingFrustum& frustum);
    LODLevel CalculateLOD(float distance) const;
    
    std::unique_ptr<QuadTreeNode> mRoot;
    std::vector<QuadTreeRenderNode> mVisibleNodes;
    std::vector<float> mLodDistances;  // Расстояния переключения LOD
    float mTerrainSize;
    int mMaxDepth;
};
