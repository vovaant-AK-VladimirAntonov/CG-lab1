#include "QuadTree.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

QuadTree::QuadTree()
    : mTerrainSize(0), mMaxDepth(0)
{
}

QuadTree::~QuadTree()
{
}

void QuadTree::Initialize(float terrainSize, int maxDepth, 
                          const std::vector<float>& lodDistances)
{
    mTerrainSize = terrainSize;
    mMaxDepth = maxDepth;
    mLodDistances = lodDistances;
    
    // Создаём корневой узел
    mRoot = std::make_unique<QuadTreeNode>();
    
    // Строим дерево рекурсивно
    BuildTree(mRoot.get(), 0, 0, terrainSize, 0);
}

void QuadTree::BuildTree(QuadTreeNode* node, float x, float z, float size, int depth)
{
    node->Size = size;
    node->Depth = depth;
    node->Center = XMFLOAT3(x + size * 0.5f, 0, z + size * 0.5f);
    
    // Создаём BoundingBox (высота террейна 0-500)
    float minY = 0.0f;
    float maxY = 500.0f;
    node->Bounds = BoundingBox(
        XMFLOAT3(x + size * 0.5f, (minY + maxY) * 0.5f, z + size * 0.5f),
        XMFLOAT3(size * 0.5f, (maxY - minY) * 0.5f, size * 0.5f)
    );
    
    // Текстурные координаты (0-1 по всему террейну)
    node->TexCoordMin = XMFLOAT2(x / mTerrainSize, 1.0f - (z + size) / mTerrainSize);
    node->TexCoordMax = XMFLOAT2((x + size) / mTerrainSize, 1.0f - z / mTerrainSize);
    
    // Если достигли максимальной глубины - это лист
    if (depth >= mMaxDepth)
    {
        node->IsLeaf = true;
        return;
    }
    
    node->IsLeaf = false;
    float halfSize = size * 0.5f;
    
    // Создаём 4 дочерних узла (NW, NE, SW, SE)
    // NW (North-West) - верхний левый
    node->Children[0] = std::make_unique<QuadTreeNode>();
    BuildTree(node->Children[0].get(), x, z, halfSize, depth + 1);
    
    // NE (North-East) - верхний правый
    node->Children[1] = std::make_unique<QuadTreeNode>();
    BuildTree(node->Children[1].get(), x + halfSize, z, halfSize, depth + 1);
    
    // SW (South-West) - нижний левый
    node->Children[2] = std::make_unique<QuadTreeNode>();
    BuildTree(node->Children[2].get(), x, z + halfSize, halfSize, depth + 1);
    
    // SE (South-East) - нижний правый
    node->Children[3] = std::make_unique<QuadTreeNode>();
    BuildTree(node->Children[3].get(), x + halfSize, z + halfSize, halfSize, depth + 1);
}

void QuadTree::Update(const XMFLOAT3& cameraPos, const BoundingFrustum& frustum)
{
    mVisibleNodes.clear();
    
    if (mRoot)
    {
        UpdateNode(mRoot.get(), cameraPos, frustum);
    }
}

void QuadTree::UpdateNode(QuadTreeNode* node, const XMFLOAT3& cameraPos,
                          const BoundingFrustum& frustum)
{
    // Frustum Culling - проверяем видимость узла
    if (frustum.Contains(node->Bounds) == DISJOINT)
    {
        return; // Узел не видим - пропускаем
    }
    
    // Вычисляем расстояние от камеры до центра узла
    float dx = node->Center.x - cameraPos.x;
    float dy = node->Center.y - cameraPos.y;
    float dz = node->Center.z - cameraPos.z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    
    // Определяем LOD на основе расстояния
    LODLevel lod = CalculateLOD(distance);
    node->CurrentLOD = lod;
    
    // Определяем, нужно ли разбивать узел дальше
    // Разбиваем если: не лист И LOD требует большей детализации чем текущая глубина
    int requiredDepth = static_cast<int>(lod);
    bool shouldSubdivide = !node->IsLeaf && (mMaxDepth - node->Depth) > requiredDepth;
    
    if (shouldSubdivide)
    {
        // Рекурсивно обрабатываем дочерние узлы
        for (int i = 0; i < 4; i++)
        {
            if (node->Children[i])
            {
                UpdateNode(node->Children[i].get(), cameraPos, frustum);
            }
        }
    }
    else
    {
        // Этот узел рендерится - добавляем в список
        QuadTreeRenderNode renderNode;
        renderNode.Node = node;
        renderNode.LOD = lod;
        renderNode.DistanceToCamera = distance;
        mVisibleNodes.push_back(renderNode);
    }
}

LODLevel QuadTree::CalculateLOD(float distance) const
{
    // Определяем LOD на основе расстояния
    // mLodDistances[0] = расстояние для LOD0->LOD1
    // mLodDistances[1] = расстояние для LOD1->LOD2
    // и т.д.
    
    for (size_t i = 0; i < mLodDistances.size(); i++)
    {
        if (distance < mLodDistances[i])
        {
            return static_cast<LODLevel>(i);
        }
    }
    
    return static_cast<LODLevel>(mLodDistances.size());
}
