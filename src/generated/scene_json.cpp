//------------------------------------------------------------------------
// GENERATED – DO NOT EDIT
//   This file was automatically generated.
//   by meta_scene_json.py
//   Generated: 2026-02-13 13:54:22
//------------------------------------------------------------------------


#include <cJSON.h>
#include "src/scene_data.h"
#include "mesh_data.h"
#include <string.h>
#include <stdio.h>

// ------------------------------------------------------------
// Serialise Scene → JSON string (caller must free with cJSON_free)
// ------------------------------------------------------------
char* scene_to_json(const Scene* scene) {
    cJSON* root = cJSON_CreateObject();

    // objectCount
    cJSON_AddNumberToObject(root, "objectCount", scene->objectCount);

    // objects array
    cJSON* objectsArray = cJSON_CreateArray();
    for (int i = 0; i < scene->objectCount; ++i) {
        const SceneObject* obj = &scene->objects[i];
        cJSON* objJson = cJSON_CreateObject();
        cJSON_AddStringToObject(objJson, "nametag", obj->nametag);
        cJSON* posArr = cJSON_CreateFloatArray((float*)&obj->pos, 3);
        cJSON_AddItemToObject(objJson, "pos", posArr);
        cJSON* rotArr = cJSON_CreateFloatArray((float*)&obj->rot, 4);
        cJSON_AddItemToObject(objJson, "rot", rotArr);
        cJSON* scaleArr = cJSON_CreateFloatArray((float*)&obj->scale, 3);
        cJSON_AddItemToObject(objJson, "scale", scaleArr);
        cJSON_AddStringToObject(objJson, "type", g_primitiveNames[obj->type]);
        cJSON_AddItemToArray(objectsArray, objJson);
    }
    cJSON_AddItemToObject(root, "objects", objectsArray);

    char* result = cJSON_Print(root);
    cJSON_Delete(root);
    return result;
}

// ------------------------------------------------------------
// Parse JSON → Scene (returns 1 on success, 0 on failure)
// ------------------------------------------------------------
int scene_from_json(const char* json, Scene* scene) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return 0;

    // Clear scene first (set defaults)
    memset(scene, 0, sizeof(Scene));
    scene->objectCount = 0;

    // objectCount (optional)
    cJSON* countItem = cJSON_GetObjectItem(root, "objectCount");
    if (cJSON_IsNumber(countItem)) scene->objectCount = countItem->valueint;

    // objects array
    cJSON* objArray = cJSON_GetObjectItem(root, "objects");
    if (cJSON_IsArray(objArray)) {
        int arraySize = cJSON_GetArraySize(objArray);
        for (int i = 0; i < arraySize && i < MAX_SCENE_OBJECTS; ++i) {
            cJSON* objJson = cJSON_GetArrayItem(objArray, i);
            SceneObject* obj = &scene->objects[i];
            cJSON* nametagItem = cJSON_GetObjectItem(objJson, "nametag");
            if (cJSON_IsString(nametagItem)) strncpy_s(obj->nametag, nametagItem->valuestring, sizeof(obj->nametag)-1);
            cJSON* posItem = cJSON_GetObjectItem(objJson, "pos");
            if (cJSON_IsArray(posItem) && cJSON_GetArraySize(posItem) == 3) {
                for (int j = 0; j < 3; ++j)
                    ((float*)&obj->pos)[j] = (float)cJSON_GetArrayItem(posItem, j)->valuedouble;
            }
            cJSON* rotItem = cJSON_GetObjectItem(objJson, "rot");
            if (cJSON_IsArray(rotItem) && cJSON_GetArraySize(rotItem) == 4) {
                for (int j = 0; j < 4; ++j)
                    ((float*)&obj->rot)[j] = (float)cJSON_GetArrayItem(rotItem, j)->valuedouble;
            }
            cJSON* scaleItem = cJSON_GetObjectItem(objJson, "scale");
            if (cJSON_IsArray(scaleItem) && cJSON_GetArraySize(scaleItem) == 3) {
                for (int j = 0; j < 3; ++j)
                    ((float*)&obj->scale)[j] = (float)cJSON_GetArrayItem(scaleItem, j)->valuedouble;
            }
            cJSON* typeItem = cJSON_GetObjectItem(objJson, "type");
            // Handle PrimitiveType: can be integer (old) or string (new)
            if (cJSON_IsNumber(typeItem)) {
                obj->type = (PrimitiveType)typeItem->valueint;
            } else if (cJSON_IsString(typeItem)) {
                const char* typeName = typeItem->valuestring;
                int found = -1;
                for (int idx = 0; idx < PRIMITIVE_COUNT; idx++) {
                    if (strcmp(typeName, g_primitiveNames[idx]) == 0) {
                        found = idx;
                        break;
                    }
                }
                if (found != -1) obj->type = (PrimitiveType)found;
                else {
                    obj->type = (PrimitiveType)0; // default to cube
                    fprintf(stderr, "Unknown primitive type \"%s\", defaulting to cube\n", typeName);
                }
            }
        }
        // Update objectCount if array was present
        if (arraySize > 0) scene->objectCount = arraySize;
    }

    cJSON_Delete(root);
    return 1;
}