#pragma once

#include <DirectXMath.h>
#include "scene_data.h"

// Returns true if an upright cylinder (center, radius, height) overlaps a cube (scene object)
bool OverlapCylinderCube(
    const DirectX::XMFLOAT3& cylCenter,   // world‑space center of cylinder (midpoint of height)
    float cylRadius,
    float cylHeight,
    const SceneObject& cube)               // the cube (must have objectType == OBJECT_PRIMITIVE with primitiveType == PRIMITIVE_CUBE)
{
    if (cube.objectType != OBJECT_PRIMITIVE || cube.data.primitive.primitiveType != PrimitiveType::PRIMITIVE_CUBE)
        return false;
    using namespace DirectX;

    // Load cube transform
    XMVECTOR cubePos   = XMLoadFloat3(&cube.pos);
    XMVECTOR cubeRot   = XMLoadFloat4(&cube.rot);
    XMVECTOR cubeScale = XMLoadFloat3(&cube.scale);

    // Half‑extents of the unit cube after scaling
    XMVECTOR halfExtents = XMVectorMultiply(cubeScale, XMVectorReplicate(0.5f));

    // Cube axes in world space (columns of rotation matrix)
    XMVECTOR u = XMVector3Rotate(g_XMIdentityR0, cubeRot);   // local X axis → world
    XMVECTOR v = XMVector3Rotate(g_XMIdentityR1, cubeRot);   // local Y axis → world
    XMVECTOR w = XMVector3Rotate(g_XMIdentityR2, cubeRot);   // local Z axis → world

    // Cylinder axis (upright)
    XMVECTOR cylAxis = g_XMIdentityR1;   // (0,1,0)

    // Cylinder half‑height
    float halfH = cylHeight * 0.5f;

    // Candidate separating axes:
    //   cube face normals (u, v, w),
    //   cylinder axis (cylAxis),
    //   and cross products of cylinder axis with each cube axis.
    const int MAX_AXES = 7;
    XMVECTOR axes[MAX_AXES];
    axes[0] = u;
    axes[1] = v;
    axes[2] = w;
    axes[3] = cylAxis;
    axes[4] = XMVector3Cross(cylAxis, u);
    axes[5] = XMVector3Cross(cylAxis, v);
    axes[6] = XMVector3Cross(cylAxis, w);

    const float epsilon = 1e-6f;

    for (int i = 0; i < MAX_AXES; ++i)
    {
        XMVECTOR n = axes[i];

        // Skip degenerate axes (zero length)
        float lenSq = XMVectorGetX(XMVector3LengthSq(n));
        if (lenSq < epsilon)
            continue;

        // Normalise the axis (important for correct interval lengths)
        n = XMVector3Normalize(n);

        // Project cube centre onto n
        float dO = XMVectorGetX(XMVector3Dot(cubePos, n));

        // Project cylinder centre onto n
        float dC = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&cylCenter), n));

        // Compute cube's half‑length along n
        float uDot = fabsf(XMVectorGetX(XMVector3Dot(u, n)));
        float vDot = fabsf(XMVectorGetX(XMVector3Dot(v, n)));
        float wDot = fabsf(XMVectorGetX(XMVector3Dot(w, n)));

        float halfX = XMVectorGetX(halfExtents) * uDot;
        float halfY = XMVectorGetY(halfExtents) * vDot;
        float halfZ = XMVectorGetZ(halfExtents) * wDot;
        float Lcube = halfX + halfY + halfZ;

        // Compute cylinder's half‑length along n
        float aDot = fabsf(XMVectorGetX(XMVector3Dot(cylAxis, n)));
        // Clamp aDot to [0,1] to avoid tiny numerical errors
        if (aDot > 1.0f) aDot = 1.0f;

        float radial = sqrtf(1.0f - aDot * aDot);
        float Lcyl = halfH * aDot + cylRadius * radial;

        // Separation test
        float delta = fabsf(dC - dO);
        if (delta > Lcyl + Lcube + epsilon)
            return false;   // separating axis found → no overlap
    }

    // No separating axis -> we are overlapping
    return true;
}