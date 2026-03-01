#pragma once

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <windows.h>
#include <directx/d3d12.h>

#pragma warning(pop)
#include "scene_data.h"

bool IntersectRayCube(const DirectX::XMFLOAT3 &rayOrigin, const DirectX::XMFLOAT3 &rayDir, const SceneObject &cube, float &tMin, float &tMax)
{
    DirectX::XMVECTOR O = DirectX::XMLoadFloat3(&rayOrigin);
    DirectX::XMVECTOR D = DirectX::XMLoadFloat3(&rayDir);
    DirectX::XMVECTOR P = DirectX::XMLoadFloat3(&cube.pos);
    DirectX::XMVECTOR Q = DirectX::XMLoadFloat4(&cube.rot);
    DirectX::XMVECTOR S = DirectX::XMLoadFloat3(&cube.scale);

    // Avoid division by zero
    DirectX::XMVECTOR zero = DirectX::XMVectorZero();
    if (DirectX::XMVector3Equal(S, zero))
        return false;

    DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(Q);
    DirectX::XMMATRIX R_T = DirectX::XMMatrixTranspose(R); // inverse rotation

    // Transform ray to cube's local space (unit cube centered at origin)
    DirectX::XMVECTOR O_rel = DirectX::XMVectorSubtract(O, P);
    DirectX::XMVECTOR O_local = DirectX::XMVectorDivide(XMVector3Transform(O_rel, R_T), S);
    DirectX::XMVECTOR D_local = DirectX::XMVectorDivide(XMVector3Transform(D, R_T), S);

    float o[3], d[3];
    DirectX::XMStoreFloat3((DirectX::XMFLOAT3 *)o, O_local);
    DirectX::XMStoreFloat3((DirectX::XMFLOAT3 *)d, D_local);

    const float half = 0.5f;
    const float epsilon = 1e-6f;

    tMin = -FLT_MAX;
    tMax = FLT_MAX;

    for (int i = 0; i < 3; ++i)
    {
        if (fabsf(d[i]) < epsilon)
        {
            // Ray parallel to slab – must be inside
            if (o[i] < -half || o[i] > half)
                return false;
        }
        else
        {
            float t1 = (-half - o[i]) / d[i];
            float t2 = (half - o[i]) / d[i];
            if (t1 > t2)
            {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }

            if (t1 > tMin)
                tMin = t1;
            if (t2 < tMax)
                tMax = t2;
            if (tMin > tMax)
                return false;
        }
    }
    return true;
}

bool IntersectRayCylinder(const DirectX::XMFLOAT3 &rayOrigin, const DirectX::XMFLOAT3 &rayDir, const SceneObject &cylinder, float &tMin, float &tMax)
{
    using namespace DirectX;

    XMVECTOR O = XMLoadFloat3(&rayOrigin);
    XMVECTOR D = XMLoadFloat3(&rayDir);
    XMVECTOR P = XMLoadFloat3(&cylinder.pos);
    XMVECTOR Q = XMLoadFloat4(&cylinder.rot);
    XMVECTOR S = XMLoadFloat3(&cylinder.scale);

    // Reject degenerate scale
    XMVECTOR zero = XMVectorZero();
    if (XMVector3Equal(S, zero))
        return false;

    XMMATRIX R = XMMatrixRotationQuaternion(Q);
    XMMATRIX R_T = XMMatrixTranspose(R); // inverse rotation

    // Transform ray to local space (unit cylinder: radius 0.5, height 1)
    XMVECTOR O_rel = XMVectorSubtract(O, P);
    XMVECTOR O_local = XMVectorDivide(XMVector3Transform(O_rel, R_T), S);
    XMVECTOR D_local = XMVectorDivide(XMVector3Transform(D, R_T), S);

    float o[3], d[3];
    XMStoreFloat3((XMFLOAT3 *)o, O_local);
    XMStoreFloat3((XMFLOAT3 *)d, D_local);

    const float r = 0.5f;
    const float halfH = 0.5f;
    const float epsilon = 1e-6f;

    // We'll collect up to 4 intersection t values (2 caps + up to 2 side)
    float hits[4];
    int hitCount = 0;

    // ----- Caps (y = ±halfH) -----
    for (int sign = -1; sign <= 1; sign += 2)
    {
        float y_plane = sign * halfH;
        if (fabsf(d[1]) > epsilon)
        {
            float t = (y_plane - o[1]) / d[1];
            float x = o[0] + t * d[0];
            float z = o[2] + t * d[2];
            if (x * x + z * z <= r * r + epsilon)
                hits[hitCount++] = t;
        }
    }

    // ----- Cylinder side -----
    float a = d[0] * d[0] + d[2] * d[2];
    if (fabsf(a) > epsilon)
    {
        float b = 2.0f * (o[0] * d[0] + o[2] * d[2]);
        float c = o[0] * o[0] + o[2] * o[2] - r * r;
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f)
        {
            disc = sqrtf(disc);
            float t1 = (-b - disc) / (2.0f * a);
            float t2 = (-b + disc) / (2.0f * a);
            for (float t : {t1, t2})
            {
                float y = o[1] + t * d[1];
                if (y >= -halfH - epsilon && y <= halfH + epsilon)
                    hits[hitCount++] = t;
            }
        }
    }
    else
    {
        // Ray is parallel to cylinder axis; if it's within radius, the side intersection is infinite.
        // We already handled caps, so nothing more to add.
    }

    if (hitCount == 0)
        return false;

    // Sort hits (simple bubble sort, max 4 elements)
    for (int i = 0; i < hitCount - 1; ++i)
        for (int j = i + 1; j < hitCount; ++j)
            if (hits[i] > hits[j])
            {
                float tmp = hits[i];
                hits[i] = hits[j];
                hits[j] = tmp;
            }

    tMin = hits[0];
    tMax = hits[hitCount - 1];
    return true;
}

//TODO always return false origin is inside the sphere
bool IntersectRaySphere(const DirectX::XMFLOAT3 &rayOrigin, const DirectX::XMFLOAT3 &rayDir, const SceneObject &sphere, float &tMin, float &tMax)
{
    using namespace DirectX;

    XMVECTOR O = XMLoadFloat3(&rayOrigin);
    XMVECTOR D = XMLoadFloat3(&rayDir);
    XMVECTOR P = XMLoadFloat3(&sphere.pos);
    XMVECTOR Q = XMLoadFloat4(&sphere.rot);
    XMVECTOR S = XMLoadFloat3(&sphere.scale);

    // Reject degenerate scale
    XMVECTOR zero = XMVectorZero();
    if (XMVector3Equal(S, zero))
        return false;

    XMMATRIX R = XMMatrixRotationQuaternion(Q);
    XMMATRIX R_T = XMMatrixTranspose(R); // inverse rotation

    // Transform ray to local space (unit sphere radius 0.5)
    XMVECTOR O_rel = XMVectorSubtract(O, P);
    XMVECTOR O_local = XMVectorDivide(XMVector3Transform(O_rel, R_T), S);
    XMVECTOR D_local = XMVectorDivide(XMVector3Transform(D, R_T), S);

    float o[3], d[3];
    XMStoreFloat3((XMFLOAT3 *)o, O_local);
    XMStoreFloat3((XMFLOAT3 *)d, D_local);

    const float r = 0.5f;
    const float epsilon = 1e-6f;

    // Quadratic coefficients: a t^2 + b t + c = 0
    float a = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (fabsf(a) < epsilon)
        return false; // degenerate direction (shouldn't happen)

    float b = 2.0f * (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]);
    float c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - r * r;

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return false;

    disc = sqrtf(disc);
    float t1 = (-b - disc) / (2.0f * a);
    float t2 = (-b + disc) / (2.0f * a);

    // Sort so t1 ≤ t2
    if (t1 > t2)
    {
        float tmp = t1;
        t1 = t2;
        t2 = tmp;
    }

    tMin = t1;
    tMax = t2;
    return true;
}

bool IntersectRayPrism(const DirectX::XMFLOAT3 &rayOrigin, const DirectX::XMFLOAT3 &rayDir, const SceneObject &prism, float &tMin, float &tMax)
{
    using namespace DirectX;

    XMVECTOR O = XMLoadFloat3(&rayOrigin);
    XMVECTOR D = XMLoadFloat3(&rayDir);
    XMVECTOR P = XMLoadFloat3(&prism.pos);
    XMVECTOR Q = XMLoadFloat4(&prism.rot);
    XMVECTOR S = XMLoadFloat3(&prism.scale);

    if (XMVector3Equal(S, XMVectorZero()))
        return false;

    XMMATRIX R = XMMatrixRotationQuaternion(Q);
    XMMATRIX R_T = XMMatrixTranspose(R); // inverse rotation

    XMVECTOR O_rel = XMVectorSubtract(O, P);
    XMVECTOR O_local = XMVectorDivide(XMVector3Transform(O_rel, R_T), S);
    XMVECTOR D_local = XMVectorDivide(XMVector3Transform(D, R_T), S);

    float o[3], d[3];
    XMStoreFloat3((XMFLOAT3 *)o, O_local);
    XMStoreFloat3((XMFLOAT3 *)d, D_local);

    // Unique vertices of the prism in local space (from your mesh data)
    const XMFLOAT3 v0 = {0.0f, -0.5f, 0.5f};
    const XMFLOAT3 v1 = {-0.433013f, -0.5f, -0.25f};
    const XMFLOAT3 v2 = {0.433013f, -0.5f, -0.25f};
    const XMFLOAT3 v3 = {0.0f, 0.5f, 0.5f};
    const XMFLOAT3 v4 = {-0.433013f, 0.5f, -0.25f};
    const XMFLOAT3 v5 = {0.433013f, 0.5f, -0.25f};

    // Face definitions (convex polygons)
    const XMFLOAT3 *bottomTri[3] = {&v0, &v1, &v2};
    const XMFLOAT3 *topTri[3] = {&v3, &v5, &v4};
    const XMFLOAT3 *side0[4] = {&v0, &v1, &v4, &v3};
    const XMFLOAT3 *side1[4] = {&v1, &v2, &v5, &v4};
    const XMFLOAT3 *side2[4] = {&v2, &v0, &v3, &v5};

    struct Face
    {
        const XMFLOAT3 **verts;
        int count;
    };
    Face faces[5] = {
        {bottomTri, 3},
        {topTri, 3},
        {side0, 4},
        {side1, 4},
        {side2, 4}};

    const float epsilon = 1e-5f;
    float tHits[10];
    int hitCount = 0;

    for (int faceIdx = 0; faceIdx < 5; ++faceIdx)
    {
        const Face &f = faces[faceIdx];
        int n = f.count;

        // Compute plane normal from first three vertices
        XMVECTOR p0 = XMLoadFloat3(f.verts[0]);
        XMVECTOR p1 = XMLoadFloat3(f.verts[1]);
        XMVECTOR p2 = XMLoadFloat3(f.verts[2]);
        XMVECTOR e1 = XMVectorSubtract(p1, p0);
        XMVECTOR e2 = XMVectorSubtract(p2, p0);
        XMVECTOR N = XMVector3Normalize(XMVector3Cross(e1, e2));

        // Ray-plane intersection
        XMVECTOR O_vec = XMLoadFloat3((XMFLOAT3 *)o);
        XMVECTOR D_vec = XMLoadFloat3((XMFLOAT3 *)d);
        float denom = XMVectorGetX(XMVector3Dot(D_vec, N));
        if (fabsf(denom) < epsilon)
            continue;

        float t = XMVectorGetX(XMVector3Dot(XMVectorSubtract(p0, O_vec), N)) / denom;
        if (t < -epsilon)
            continue;

        XMVECTOR P_local = XMVectorAdd(O_vec, XMVectorScale(D_vec, t));
        XMFLOAT3 p;
        XMStoreFloat3(&p, P_local);

        // Point-in-polygon test
        bool inside = false;
        if (n == 3) // triangle cap
        {
            // Barycentric test
            XMVECTOR v0v1 = XMVectorSubtract(p1, p0);
            XMVECTOR v0v2 = XMVectorSubtract(p2, p0);
            XMVECTOR v0p = XMVectorSubtract(P_local, p0);

            float d00 = XMVectorGetX(XMVector3Dot(v0v1, v0v1));
            float d01 = XMVectorGetX(XMVector3Dot(v0v1, v0v2));
            float d02 = XMVectorGetX(XMVector3Dot(v0v1, v0p));
            float d11 = XMVectorGetX(XMVector3Dot(v0v2, v0v2));
            float d12 = XMVectorGetX(XMVector3Dot(v0v2, v0p));

            float invDenom = 1.0f / (d00 * d11 - d01 * d01);
            float u = (d11 * d02 - d01 * d12) * invDenom;
            float v = (d00 * d12 - d01 * d02) * invDenom;

            inside = (u >= -epsilon) && (v >= -epsilon) && (u + v <= 1.0f + epsilon);
        }
        else // quad side
        {
            // Compute centroid to determine inward edge normals
            XMVECTOR centroid = XMVectorZero();
            for (int i = 0; i < n; ++i)
                centroid = XMVectorAdd(centroid, XMLoadFloat3(f.verts[i]));
            centroid = XMVectorScale(centroid, 1.0f / n);

            inside = true;
            for (int i = 0; i < n; ++i)
            {
                int j = (i + 1) % n;
                XMVECTOR vi = XMLoadFloat3(f.verts[i]);
                XMVECTOR vj = XMLoadFloat3(f.verts[j]);
                XMVECTOR edge = XMVectorSubtract(vj, vi);
                XMVECTOR edgeNormal = XMVector3Normalize(XMVector3Cross(edge, N));

                // Flip if necessary so that edgeNormal points toward centroid
                XMVECTOR toCentroid = XMVectorSubtract(centroid, vi);
                if (XMVectorGetX(XMVector3Dot(toCentroid, edgeNormal)) < 0)
                    edgeNormal = XMVectorNegate(edgeNormal);

                XMVECTOR toPoint = XMVectorSubtract(P_local, vi);
                if (XMVectorGetX(XMVector3Dot(toPoint, edgeNormal)) < -epsilon)
                {
                    inside = false;
                    break;
                }
            }
        }

        if (inside)
            tHits[hitCount++] = t;
    }

    if (hitCount == 0)
        return false;

    tMin = tHits[0];
    tMax = tHits[0];
    for (int i = 1; i < hitCount; ++i)
    {
        if (tHits[i] < tMin)
            tMin = tHits[i];
        if (tHits[i] > tMax)
            tMax = tHits[i];
    }
    return true;
}