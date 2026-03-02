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

// Returns true if the upright cylinder overlaps the cube, and fills outNormal (unit vector from cube to cylinder)
// and outPenetration (distance to push cylinder out along that normal).
bool OverlapCylinderCubeContact(
    const DirectX::XMFLOAT3& cylCenter,
    float cylRadius,
    float cylHeight,
    const SceneObject& cube,
    DirectX::XMFLOAT3& outNormal,
    float& outPenetration)
{
    if (cube.objectType != OBJECT_PRIMITIVE || cube.data.primitive.primitiveType != PrimitiveType::PRIMITIVE_CUBE)
        return false;

    using namespace DirectX;

    XMVECTOR cubePos   = XMLoadFloat3(&cube.pos);
    XMVECTOR cubeRot   = XMLoadFloat4(&cube.rot);
    XMVECTOR cubeScale = XMLoadFloat3(&cube.scale);

    XMVECTOR halfExtents = XMVectorMultiply(cubeScale, XMVectorReplicate(0.5f));

    // Cube axes in world space
    XMVECTOR u = XMVector3Rotate(g_XMIdentityR0, cubeRot);
    XMVECTOR v = XMVector3Rotate(g_XMIdentityR1, cubeRot);
    XMVECTOR w = XMVector3Rotate(g_XMIdentityR2, cubeRot);

    XMVECTOR cylAxis = g_XMIdentityR1;   // (0,1,0) upright

    float halfH = cylHeight * 0.5f;

    // Candidate axes: cube face normals, cylinder axis, and cross products.
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
    float minOverlap = FLT_MAX;
    XMVECTOR bestAxis = XMVectorZero();
    bool foundOverlap = false;

    for (int i = 0; i < MAX_AXES; ++i)
    {
        XMVECTOR n = axes[i];
        float lenSq = XMVectorGetX(XMVector3LengthSq(n));
        if (lenSq < epsilon)
            continue;

        n = XMVector3Normalize(n);

        // Project cube centre onto n
        float dO = XMVectorGetX(XMVector3Dot(cubePos, n));

        // Project cylinder centre onto n
        float dC = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&cylCenter), n));

        // Cube half‑length along n
        float uDot = fabsf(XMVectorGetX(XMVector3Dot(u, n)));
        float vDot = fabsf(XMVectorGetX(XMVector3Dot(v, n)));
        float wDot = fabsf(XMVectorGetX(XMVector3Dot(w, n)));
        float halfX = XMVectorGetX(halfExtents) * uDot;
        float halfY = XMVectorGetY(halfExtents) * vDot;
        float halfZ = XMVectorGetZ(halfExtents) * wDot;
        float Lcube = halfX + halfY + halfZ;

        // Cylinder half‑length along n
        float aDot = fabsf(XMVectorGetX(XMVector3Dot(cylAxis, n)));
        if (aDot > 1.0f) aDot = 1.0f;               // clamp due to FP error
        float radial = sqrtf(1.0f - aDot * aDot);
        float Lcyl = halfH * aDot + cylRadius * radial;

        float delta = fabsf(dC - dO);
        if (delta > Lcyl + Lcube + epsilon)
            return false;   // separating axis found → no overlap

        float overlap = Lcyl + Lcube - delta;
        if (overlap < minOverlap)
        {
            minOverlap = overlap;
            bestAxis = n;
            // Determine sign: direction from cube to cylinder
            // If dC > dO, cylinder is on the + side of the axis, so normal = +n.
            float sign = (dC > dO) ? 1.0f : -1.0f;
            bestAxis = XMVectorMultiply(bestAxis, XMVectorReplicate(sign));
            foundOverlap = true;
        }
    }

    if (!foundOverlap)
        return false;   // should not happen if all axes passed, but safety

    XMStoreFloat3(&outNormal, bestAxis);
    outPenetration = minOverlap;
    return true;
}

// Returns true if the upright cylinder overlaps the sphere, and fills outNormal (unit vector from sphere to cylinder)
// and outPenetration (distance to push cylinder out along that normal).
bool OverlapCylinderSphereContact(
    const DirectX::XMFLOAT3& cylCenter,   // world‑space centre of cylinder (midpoint of height)
    float cylRadius,
    float cylHeight,
    const SceneObject& sphere,            // must be OBJECT_PRIMITIVE with PRIMITIVE_SPHERE (assumed uniform scale)
    DirectX::XMFLOAT3& outNormal,
    float& outPenetration)
{
    if (sphere.objectType != OBJECT_PRIMITIVE || sphere.data.primitive.primitiveType != PrimitiveType::PRIMITIVE_SPHERE)
        return false;

    using namespace DirectX;

    // Sphere centre and radius (assume uniform scale)
    XMVECTOR spherePos = XMLoadFloat3(&sphere.pos);
    float sphereRadius = sphere.scale.x * 0.5f;   // unit sphere radius 0.5 in local space, scaled uniformly

    // Cylinder geometry
    XMVECTOR C = XMLoadFloat3(&cylCenter);
    XMVECTOR axis = g_XMIdentityR1;               // (0,1,0) upright
    float halfH = cylHeight * 0.5f;

    // Vector from cylinder centre to sphere centre
    XMVECTOR v = XMVectorSubtract(spherePos, C);
    float t = XMVectorGetX(XMVector3Dot(v, axis));

    // Candidate points on cylinder surface (side, top cap, bottom cap)
    XMVECTOR bestPoint = XMVectorZero();
    float bestDistSq = FLT_MAX;

    // ---- Side ----
    {
        float tClamped = max(-halfH, min(halfH, t));
        XMVECTOR pointOnAxis = XMVectorAdd(C, XMVectorScale(axis, tClamped));
        XMVECTOR radial = XMVectorSubtract(spherePos, pointOnAxis);
        float radialLen = XMVectorGetX(XMVector3Length(radial));
        if (radialLen > 1e-6f)
        {
            XMVECTOR dir = XMVectorScale(radial, 1.0f / radialLen);
            XMVECTOR pointSide = XMVectorAdd(pointOnAxis, XMVectorScale(dir, cylRadius));
            float dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(spherePos, pointSide)));
            if (dsq < bestDistSq)
            {
                bestDistSq = dsq;
                bestPoint = pointSide;
            }
        }
        else
        {
            // Sphere centre exactly on axis – pick an arbitrary horizontal direction
            XMVECTOR pointSide = XMVectorAdd(pointOnAxis, XMVectorSet(cylRadius, 0, 0, 0));
            float dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(spherePos, pointSide)));
            if (dsq < bestDistSq)
            {
                bestDistSq = dsq;
                bestPoint = pointSide;
            }
        }
    }

    // ---- Top cap ----
    {
        XMVECTOR topCentre = XMVectorAdd(C, XMVectorScale(axis, halfH));
        XMVECTOR toSphere = XMVectorSubtract(spherePos, topCentre);
        float axial = XMVectorGetX(XMVector3Dot(toSphere, axis));
        XMVECTOR radial = XMVectorSubtract(toSphere, XMVectorScale(axis, axial));
        float radialLen = XMVectorGetX(XMVector3Length(radial));
        XMVECTOR pointTop;
        if (radialLen <= cylRadius)
        {
            pointTop = XMVectorAdd(topCentre, radial);
        }
        else
        {
            pointTop = XMVectorAdd(topCentre, XMVectorScale(radial, cylRadius / radialLen));
        }
        float dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(spherePos, pointTop)));
        if (dsq < bestDistSq)
        {
            bestDistSq = dsq;
            bestPoint = pointTop;
        }
    }

    // ---- Bottom cap ----
    {
        XMVECTOR bottomCentre = XMVectorSubtract(C, XMVectorScale(axis, halfH));
        XMVECTOR toSphere = XMVectorSubtract(spherePos, bottomCentre);
        float axial = XMVectorGetX(XMVector3Dot(toSphere, axis));
        XMVECTOR radial = XMVectorSubtract(toSphere, XMVectorScale(axis, axial));
        float radialLen = XMVectorGetX(XMVector3Length(radial));
        XMVECTOR pointBottom;
        if (radialLen <= cylRadius)
        {
            pointBottom = XMVectorAdd(bottomCentre, radial);
        }
        else
        {
            pointBottom = XMVectorAdd(bottomCentre, XMVectorScale(radial, cylRadius / radialLen));
        }
        float dsq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(spherePos, pointBottom)));
        if (dsq < bestDistSq)
        {
            bestDistSq = dsq;
            bestPoint = pointBottom;
        }
    }

    // Check overlap
    float dist = sqrtf(bestDistSq);
    if (dist >= sphereRadius)
        return false;   // no overlap

    // Compute normal (from sphere to cylinder) and penetration
    XMVECTOR toSphere = XMVectorSubtract(spherePos, bestPoint);
    float len = XMVectorGetX(XMVector3Length(toSphere));
    if (len > 1e-6f)
    {
        outNormal.x = -XMVectorGetX(toSphere) / len;
        outNormal.y = -XMVectorGetY(toSphere) / len;
        outNormal.z = -XMVectorGetZ(toSphere) / len;
    }
    else
    {
        // sphere centre exactly on cylinder surface – arbitrary normal
        outNormal = DirectX::XMFLOAT3(1, 0, 0);
    }
    outPenetration = sphereRadius - dist;

    return true;
}