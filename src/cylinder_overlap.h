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

bool OverlapCylinderSphereContactNonUniformScale(
    const DirectX::XMFLOAT3& cylCenter,
    float cylRadius,
    float cylHeight,
    const SceneObject& sphere,
    DirectX::XMFLOAT3& outNormal,
    float& outPenetration)
{
    if (sphere.objectType != OBJECT_PRIMITIVE || sphere.data.primitive.primitiveType != PrimitiveType::PRIMITIVE_SPHERE)
        return false;

    using namespace DirectX;

    XMVECTOR spherePos = XMLoadFloat3(&sphere.pos);
    XMVECTOR sphereRot = XMLoadFloat4(&sphere.rot);
    XMVECTOR sphereScale = XMLoadFloat3(&sphere.scale);

    // Build matrix M = diag(1/scale) * R^T  (world → ellipsoid local space)
    XMMATRIX R = XMMatrixRotationQuaternion(sphereRot);
    XMMATRIX invScale = XMMatrixScaling(1.0f / sphere.scale.x, 1.0f / sphere.scale.y, 1.0f / sphere.scale.z);
    XMMATRIX M = XMMatrixMultiply(invScale, XMMatrixTranspose(R));

    // Transform cylinder centre to local space
    XMVECTOR C_local = XMVector3Transform(XMVectorSubtract(XMLoadFloat3(&cylCenter), spherePos), M);

    // Transform cylinder basis vectors to local space
    XMVECTOR axis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR e1 = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR e2 = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMVECTOR a = XMVector3TransformNormal(axis, M);
    XMVECTOR b = XMVector3TransformNormal(e1, M);
    XMVECTOR c = XMVector3TransformNormal(e2, M);

    float halfH = cylHeight * 0.5f;
    float radius = cylRadius;

    // Normalized parameter ranges
    float u_min = -halfH, u_max = halfH;
    float vw_max = radius;

    // Start at centre of cylinder (u=0, v=0, w=0)
    float u_norm = 0.0f, v_norm = 0.0f, w_norm = 0.0f;
    const int maxIter = 30;
    const float step = 0.2f; // step in normalized space

    for (int iter = 0; iter < maxIter; ++iter)
    {
        // Convert normalized → actual
        float u = u_norm * halfH;
        float v = v_norm * radius;
        float w = w_norm * radius;

        XMVECTOR p = XMVectorAdd(C_local,
                      XMVectorAdd(XMVectorScale(a, u),
                      XMVectorAdd(XMVectorScale(b, v), XMVectorScale(c, w))));

        // Gradient of |p|^2 w.r.t actual parameters
        float gu_actual = 2.0f * XMVectorGetX(XMVector3Dot(p, a));
        float gv_actual = 2.0f * XMVectorGetX(XMVector3Dot(p, b));
        float gw_actual = 2.0f * XMVectorGetX(XMVector3Dot(p, c));

        // Convert to normalized gradient (chain rule)
        float gu_norm = gu_actual * halfH;
        float gv_norm = gv_actual * radius;
        float gw_norm = gw_actual * radius;

        // Step in negative gradient direction
        u_norm -= step * gu_norm;
        v_norm -= step * gv_norm;
        w_norm -= step * gw_norm;

        // Project constraints in normalized space
        u_norm = max(-1.0f, min(1.0f, u_norm));
        float vw_len_norm = sqrtf(v_norm * v_norm + w_norm * w_norm);
        if (vw_len_norm > 1.0f)
        {
            v_norm /= vw_len_norm;
            w_norm /= vw_len_norm;
        }
    }

    // Convert final normalized to actual
    float u_final = u_norm * halfH;
    float v_final = v_norm * radius;
    float w_final = w_norm * radius;

    XMVECTOR p_local = XMVectorAdd(C_local,
                       XMVectorAdd(XMVectorScale(a, u_final),
                       XMVectorAdd(XMVectorScale(b, v_final), XMVectorScale(c, w_final))));

    float dist_local = XMVectorGetX(XMVector3Length(p_local));
    if (dist_local >= 0.5f)
        return false;

    // Transform point to world space
    XMMATRIX S = XMMatrixScalingFromVector(sphereScale);
    XMMATRIX worldFromLocal = XMMatrixMultiply(S, R);
    XMVECTOR p_world = XMVector3Transform(p_local, worldFromLocal);
    float worldDist = XMVectorGetX(XMVector3Length(p_world));

    // Normal from sphere centre to cylinder (world space)
    XMVECTOR worldNormal = XMVectorScale(p_world, 1.0f / worldDist);
    XMStoreFloat3(&outNormal, worldNormal);

    // Ellipsoid radius along that normal
    XMFLOAT3 n;
    XMStoreFloat3(&n, XMLoadFloat3(&outNormal));
    float invRadSq = (n.x / sphere.scale.x) * (n.x / sphere.scale.x) +
                     (n.y / sphere.scale.y) * (n.y / sphere.scale.y) +
                     (n.z / sphere.scale.z) * (n.z / sphere.scale.z);
    float surfaceDist = 0.5f / sqrtf(invRadSq);
    outPenetration = surfaceDist - worldDist;

    return true;
}