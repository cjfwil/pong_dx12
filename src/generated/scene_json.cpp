//------------------------------------------------------------------------
// GENERATED – DO NOT EDIT
//   This file was automatically generated.
//   by meta_scene_json.py
//   Generated: 2026-02-21 09:18:51
//------------------------------------------------------------------------


#include <cJSON.h>
#include "src/scene_data.h"
#include "mesh_data.h"
#include "render_pipeline_data.h"
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

        // Common fields
        cJSON_AddStringToObject(objJson, "nametag", obj->nametag);

        cJSON* posArr = cJSON_CreateFloatArray((float*)&obj->pos, 3);
        cJSON_AddItemToObject(objJson, "pos", posArr);

        cJSON* rotArr = cJSON_CreateFloatArray((float*)&obj->rot, 4);
        cJSON_AddItemToObject(objJson, "rot", rotArr);

        cJSON* scaleArr = cJSON_CreateFloatArray((float*)&obj->scale, 3);
        cJSON_AddItemToObject(objJson, "scale", scaleArr);

        cJSON_AddStringToObject(objJson, "objectType", g_objectTypeNames[obj->objectType]);
        cJSON_AddStringToObject(objJson, "pipeline", g_renderPipelineNames[obj->pipeline]);

        // Type‑specific data
        switch (obj->objectType) {
            case OBJECT_PRIMITIVE: {
                cJSON* primitiveData = cJSON_CreateObject();
            cJSON_AddStringToObject(primitiveData, "primitiveType", g_primitiveNames[obj->data.primitive.primitiveType]);
                cJSON_AddItemToObject(objJson, "primitiveData", primitiveData);
                break;
            }
            case OBJECT_HEIGHTFIELD: {
                cJSON* heightfieldData = cJSON_CreateObject();
            cJSON_AddStringToObject(heightfieldData, "pathToHeightmap", obj->data.heightfield.pathToHeightmap);
            cJSON_AddNumberToObject(heightfieldData, "width", obj->data.heightfield.width);
                cJSON_AddItemToObject(objJson, "heightfieldData", heightfieldData);
                break;
            }
            case OBJECT_LOADED_MODEL: {
                cJSON* loaded_modelData = cJSON_CreateObject();
            cJSON_AddStringToObject(loaded_modelData, "pathTo", obj->data.loaded_model.pathTo);
                cJSON_AddItemToObject(objJson, "loaded_modelData", loaded_modelData);
                break;
            }
            case OBJECT_SKY_SPHERE: {
                cJSON* sky_sphereData = cJSON_CreateObject();
            cJSON_AddStringToObject(sky_sphereData, "pathToTexture", obj->data.sky_sphere.pathToTexture);
                cJSON_AddItemToObject(objJson, "sky_sphereData", sky_sphereData);
                break;
            }
            case OBJECT_WATER: {
                cJSON* waterData = cJSON_CreateObject();
            cJSON_AddNumberToObject(waterData, "choppiness", obj->data.water.choppiness);
                cJSON_AddItemToObject(objJson, "waterData", waterData);
                break;
            }
            default:
                break;
        }

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

            // Common fields
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

            cJSON* objectTypeItem = cJSON_GetObjectItem(objJson, "objectType");
            if (cJSON_IsNumber(objectTypeItem)) {
                obj->objectType = (ObjectType)objectTypeItem->valueint;
            } else if (cJSON_IsString(objectTypeItem)) {
                const char* typeName = objectTypeItem->valuestring;
                int found = -1;
                for (int idx = 0; idx < OBJECT_COUNT; idx++) {
                    if (strcmp(typeName, g_objectTypeNames[idx]) == 0) {
                        found = idx;
                        break;
                    }
                }
                if (found != -1) obj->objectType = (ObjectType)found;
                else {
                    obj->objectType = OBJECT_PRIMITIVE;
                    fprintf(stderr, "Unknown object type \"%s\", defaulting to Primitive\n", typeName);
                }
            }

            cJSON* pipelineItem = cJSON_GetObjectItem(objJson, "pipeline");
            if (cJSON_IsNumber(pipelineItem)) {
                obj->pipeline = (RenderPipeline)pipelineItem->valueint;
            } else if (cJSON_IsString(pipelineItem)) {
                const char* pipeName = pipelineItem->valuestring;
                int found = -1;
                for (int idx = 0; idx < RENDER_COUNT; idx++) {
                    if (strcmp(pipeName, g_renderPipelineNames[idx]) == 0) {
                        found = idx;
                        break;
                    }
                }
                if (found != -1) obj->pipeline = (RenderPipeline)found;
                else {
                    obj->pipeline = RENDER_DEFAULT;
                    fprintf(stderr, "Unknown pipeline \"%s\", defaulting to Default\n", pipeName);
                }
            }

            // Type‑specific data
            switch (obj->objectType) {
                case OBJECT_PRIMITIVE: {
                cJSON* primitiveData = cJSON_GetObjectItem(objJson, "primitiveData");
                if (primitiveData) {
                    cJSON* primitiveTypeItem = cJSON_GetObjectItem(primitiveData, "primitiveType");
                    if (cJSON_IsString(primitiveTypeItem)) {
                        const char* typeName = primitiveTypeItem->valuestring;
                        int found = -1;
                        for (int idx = 0; idx < PRIMITIVE_COUNT; idx++) {
                            if (strcmp(typeName, g_primitiveNames[idx]) == 0) {
                                found = idx;
                                break;
                            }
                        }
                        if (found != -1) obj->data.primitive.primitiveType = (PrimitiveType)found;
                        else {
                            obj->data.primitive.primitiveType = PRIMITIVE_CUBE;
                            fprintf(stderr, "Unknown primitive type \"%s\", defaulting to Cube\n", typeName);
                        }
                    } else if (cJSON_IsNumber(primitiveTypeItem)) {
                        obj->data.primitive.primitiveType = (PrimitiveType)primitiveTypeItem->valueint;
                    }
                }
                    break;
                }
                case OBJECT_HEIGHTFIELD: {
                cJSON* heightfieldData = cJSON_GetObjectItem(objJson, "heightfieldData");
                if (heightfieldData) {
                    cJSON* pathToHeightmapItem = cJSON_GetObjectItem(heightfieldData, "pathToHeightmap");
                    if (cJSON_IsString(pathToHeightmapItem)) strncpy_s(obj->data.heightfield.pathToHeightmap, pathToHeightmapItem->valuestring, sizeof(obj->data.heightfield.pathToHeightmap)-1);
                    cJSON* widthItem = cJSON_GetObjectItem(heightfieldData, "width");
                    if (cJSON_IsNumber(widthItem)) obj->data.heightfield.width = (uint32_t)widthItem->valuedouble;
                }
                    break;
                }
                case OBJECT_LOADED_MODEL: {
                cJSON* loaded_modelData = cJSON_GetObjectItem(objJson, "loaded_modelData");
                if (loaded_modelData) {
                    cJSON* pathToItem = cJSON_GetObjectItem(loaded_modelData, "pathTo");
                    if (cJSON_IsString(pathToItem)) strncpy_s(obj->data.loaded_model.pathTo, pathToItem->valuestring, sizeof(obj->data.loaded_model.pathTo)-1);
                }
                    break;
                }
                case OBJECT_SKY_SPHERE: {
                cJSON* sky_sphereData = cJSON_GetObjectItem(objJson, "sky_sphereData");
                if (sky_sphereData) {
                    cJSON* pathToTextureItem = cJSON_GetObjectItem(sky_sphereData, "pathToTexture");
                    if (cJSON_IsString(pathToTextureItem)) strncpy_s(obj->data.sky_sphere.pathToTexture, pathToTextureItem->valuestring, sizeof(obj->data.sky_sphere.pathToTexture)-1);
                }
                    break;
                }
                case OBJECT_WATER: {
                cJSON* waterData = cJSON_GetObjectItem(objJson, "waterData");
                if (waterData) {
                    cJSON* choppinessItem = cJSON_GetObjectItem(waterData, "choppiness");
                    if (cJSON_IsNumber(choppinessItem)) obj->data.water.choppiness = (float)choppinessItem->valuedouble;
                }
                    break;
                }
                default:
                    break;
            }
        }
        // Update objectCount if array was present
        if (arraySize > 0) scene->objectCount = arraySize;
    }

    cJSON_Delete(root);
    return 1;
}