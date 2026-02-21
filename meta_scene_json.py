#!/usr/bin/env python3
import sys
import re
from pathlib import Path
import common

def extract_union_variants(content: str) -> dict:
    # Find "struct SceneObject"
    idx = content.find("struct SceneObject")
    if idx == -1:
        common.log_error("Could not find 'struct SceneObject'")
        return {}
    # Find the opening brace after that
    brace_start = content.find('{', idx)
    if brace_start == -1:
        common.log_error("Could not find opening brace for SceneObject")
        return {}
    # Count braces to find matching closing brace
    brace_level = 1
    i = brace_start + 1
    while i < len(content):
        ch = content[i]
        if ch == '{':
            brace_level += 1
        elif ch == '}':
            brace_level -= 1
            if brace_level == 0:
                struct_body = content[brace_start+1:i].strip()
                break
        i += 1
    else:
        common.log_error("Could not find closing brace for SceneObject")
        return {}

    # Find union inside struct_body
    union_start = struct_body.find("union {")
    if union_start == -1:
        common.log_error("Could not find 'union {' in SceneObject")
        return {}
    # Count braces inside union
    brace_level = 1
    j = union_start + len("union {")
    while j < len(struct_body):
        ch = struct_body[j]
        if ch == '{':
            brace_level += 1
        elif ch == '}':
            brace_level -= 1
            if brace_level == 0:
                union_body = struct_body[union_start+len("union {"):j].strip()
                break
        j += 1
    else:
        common.log_error("Could not find closing brace for union")
        return {}

    # Parse nested structs inside union body
    variants = common.parse_nested_structs(union_body)
    return variants

def generate_scene_json(input_h: Path, output_c: Path) -> bool:
    common.log_info(f"Reading input file: {input_h}")
    if not input_h.exists():
        common.log_error(f"Input not found: {input_h}")
        return False

    with open(input_h, 'r', encoding='utf-8') as f:
        content = f.read()

    # Parse union variants
    variants = extract_union_variants(content)
    if not variants:
        common.log_error("No union variants found")
        return False

    common.log_info(f"Found variants: {list(variants.keys())}")

    def write_variant_object(variant_name, fields, obj_prefix):
        lines = []
        for typ, name, is_array, arr_sz, is_ptr in fields:
            if is_array and typ.startswith('char'):
                lines.append(f'        cJSON_AddStringToObject({variant_name}Data, "{name}", {obj_prefix}.{name});')
            else:
                if typ == 'DirectX::XMFLOAT3':
                    arr = f"{name}Arr"
                    lines.append(f'        cJSON* {arr} = cJSON_CreateFloatArray((float*)&{obj_prefix}.{name}, 3);')
                    lines.append(f'        cJSON_AddItemToObject({variant_name}Data, "{name}", {arr});')
                elif typ == 'DirectX::XMFLOAT4':
                    arr = f"{name}Arr"
                    lines.append(f'        cJSON* {arr} = cJSON_CreateFloatArray((float*)&{obj_prefix}.{name}, 4);')
                    lines.append(f'        cJSON_AddItemToObject({variant_name}Data, "{name}", {arr});')
                elif typ == 'PrimitiveType':
                    lines.append(f'        cJSON_AddStringToObject({variant_name}Data, "{name}", g_primitiveNames[{obj_prefix}.{name}]);')
                elif typ == 'RenderPipeline':
                    lines.append(f'        cJSON_AddStringToObject({variant_name}Data, "{name}", g_renderPipelineNames[{obj_prefix}.{name}]);')
                else:
                    # fallback: treat as number
                    lines.append(f'        cJSON_AddNumberToObject({variant_name}Data, "{name}", {obj_prefix}.{name});')
        return lines

    def parse_variant_object(variant_name, fields, obj_dest):
        lines = []
        lines.append(f'            cJSON* {variant_name}Data = cJSON_GetObjectItem(objJson, "{variant_name}Data");')
        lines.append(f'            if ({variant_name}Data) {{')
        for typ, name, is_array, arr_sz, is_ptr in fields:
            if is_array and typ.startswith('char'):
                lines.append(f'                cJSON* {name}Item = cJSON_GetObjectItem({variant_name}Data, "{name}");')
                lines.append(f'                if (cJSON_IsString({name}Item)) strncpy_s({obj_dest}.{name}, {name}Item->valuestring, sizeof({obj_dest}.{name})-1);')
            else:
                if typ == 'DirectX::XMFLOAT3':
                    lines.append(f'                cJSON* {name}Item = cJSON_GetObjectItem({variant_name}Data, "{name}");')
                    lines.extend([
                        f'                if (cJSON_IsArray({name}Item) && cJSON_GetArraySize({name}Item) == 3) {{',
                        f'                    for (int j = 0; j < 3; ++j)',
                        f'                        ((float*)&{obj_dest}.{name})[j] = (float)cJSON_GetArrayItem({name}Item, j)->valuedouble;',
                        f'                }}'
                    ])
                elif typ == 'DirectX::XMFLOAT4':
                    lines.append(f'                cJSON* {name}Item = cJSON_GetObjectItem({variant_name}Data, "{name}");')
                    lines.extend([
                        f'                if (cJSON_IsArray({name}Item) && cJSON_GetArraySize({name}Item) == 4) {{',
                        f'                    for (int j = 0; j < 4; ++j)',
                        f'                        ((float*)&{obj_dest}.{name})[j] = (float)cJSON_GetArrayItem({name}Item, j)->valuedouble;',
                        f'                }}'
                    ])
                elif typ == 'PrimitiveType':
                    lines.append(f'                cJSON* {name}Item = cJSON_GetObjectItem({variant_name}Data, "{name}");')
                    lines.extend([
                        f'                if (cJSON_IsString({name}Item)) {{',
                        f'                    const char* typeName = {name}Item->valuestring;',
                        f'                    int found = -1;',
                        f'                    for (int idx = 0; idx < PRIMITIVE_COUNT; idx++) {{',
                        f'                        if (strcmp(typeName, g_primitiveNames[idx]) == 0) {{',
                        f'                            found = idx;',
                        f'                            break;',
                        f'                        }}',
                        f'                    }}',
                        f'                    if (found != -1) {obj_dest}.{name} = (PrimitiveType)found;',
                        f'                    else {{',
                        f'                        {obj_dest}.{name} = PRIMITIVE_CUBE;',
                        f'                        fprintf(stderr, "Unknown primitive type \\"%s\\", defaulting to Cube\\n", typeName);',
                        f'                    }}',
                        f'                }} else if (cJSON_IsNumber({name}Item)) {{',
                        f'                    {obj_dest}.{name} = (PrimitiveType){name}Item->valueint;',
                        f'                }}'
                    ])
                else:
                    # For other numeric types, cast using the type name
                    lines.append(f'                cJSON* {name}Item = cJSON_GetObjectItem({variant_name}Data, "{name}");')
                    lines.append(f'                if (cJSON_IsNumber({name}Item)) {obj_dest}.{name} = ({typ}){name}Item->valuedouble;')
        lines.append(f'            }}')
        return lines

    # Build output C code
    header = common.make_header("meta_scene_json.py")

    lines = [
        '#include <cJSON.h>',
        '#include "src/scene_data.h"',
        '#include "mesh_data.h"',
        '#include "render_pipeline_data.h"',
        '#include <string.h>',
        '#include <stdio.h>',
        '',
        '// ------------------------------------------------------------',
        '// Serialise Scene → JSON string (caller must free with cJSON_free)',
        '// ------------------------------------------------------------',
        'char* scene_to_json(const Scene* scene) {',
        '    cJSON* root = cJSON_CreateObject();',
        '',
        '    // objectCount',
        '    cJSON_AddNumberToObject(root, "objectCount", scene->objectCount);',
        '',
        '    // objects array',
        '    cJSON* objectsArray = cJSON_CreateArray();',
        '    for (int i = 0; i < scene->objectCount; ++i) {',
        '        const SceneObject* obj = &scene->objects[i];',
        '        cJSON* objJson = cJSON_CreateObject();',
        '',
        '        // Common fields',
        '        cJSON_AddStringToObject(objJson, "nametag", obj->nametag);',
        '',
        '        cJSON* posArr = cJSON_CreateFloatArray((float*)&obj->pos, 3);',
        '        cJSON_AddItemToObject(objJson, "pos", posArr);',
        '',
        '        cJSON* rotArr = cJSON_CreateFloatArray((float*)&obj->rot, 4);',
        '        cJSON_AddItemToObject(objJson, "rot", rotArr);',
        '',
        '        cJSON* scaleArr = cJSON_CreateFloatArray((float*)&obj->scale, 3);',
        '        cJSON_AddItemToObject(objJson, "scale", scaleArr);',
        '',
        '        cJSON_AddStringToObject(objJson, "objectType", g_objectTypeNames[obj->objectType]);',
        '        cJSON_AddStringToObject(objJson, "pipeline", g_renderPipelineNames[obj->pipeline]);',
        '',
        '        // Type‑specific data',
        '        switch (obj->objectType) {',
    ]

    # Generate cases for each variant
    for variant_name, fields in variants.items():
        enum_name = f"OBJECT_{variant_name.upper()}"
        lines.append(f'            case {enum_name}: {{')
        data_obj_name = f"{variant_name}Data"
        lines.append(f'                cJSON* {data_obj_name} = cJSON_CreateObject();')
        write_lines = write_variant_object(variant_name, fields, f"obj->data.{variant_name}")
        for l in write_lines:
            lines.append('    ' + l)
        lines.append(f'                cJSON_AddItemToObject(objJson, "{variant_name}Data", {data_obj_name});')
        lines.append(f'                break;')
        lines.append(f'            }}')

    lines.extend([
        '            default:',
        '                break;',
        '        }',
        '',
        '        cJSON_AddItemToArray(objectsArray, objJson);',
        '    }',
        '    cJSON_AddItemToObject(root, "objects", objectsArray);',
        '',
        '    char* result = cJSON_Print(root);',
        '    cJSON_Delete(root);',
        '    return result;',
        '}',
        '',
        '// ------------------------------------------------------------',
        '// Parse JSON → Scene (returns 1 on success, 0 on failure)',
        '// ------------------------------------------------------------',
        'int scene_from_json(const char* json, Scene* scene) {',
        '    cJSON* root = cJSON_Parse(json);',
        '    if (!root) return 0;',
        '',
        '    // Clear scene first (set defaults)',
        '    memset(scene, 0, sizeof(Scene));',
        '    scene->objectCount = 0;',
        '',
        '    // objectCount (optional)',
        '    cJSON* countItem = cJSON_GetObjectItem(root, "objectCount");',
        '    if (cJSON_IsNumber(countItem)) scene->objectCount = countItem->valueint;',
        '',
        '    // objects array',
        '    cJSON* objArray = cJSON_GetObjectItem(root, "objects");',
        '    if (cJSON_IsArray(objArray)) {',
        '        int arraySize = cJSON_GetArraySize(objArray);',
        '        for (int i = 0; i < arraySize && i < MAX_SCENE_OBJECTS; ++i) {',
        '            cJSON* objJson = cJSON_GetArrayItem(objArray, i);',
        '            SceneObject* obj = &scene->objects[i];',
        '',
        '            // Common fields',
        '            cJSON* nametagItem = cJSON_GetObjectItem(objJson, "nametag");',
        '            if (cJSON_IsString(nametagItem)) strncpy_s(obj->nametag, nametagItem->valuestring, sizeof(obj->nametag)-1);',
        '',
        '            cJSON* posItem = cJSON_GetObjectItem(objJson, "pos");',
        '            if (cJSON_IsArray(posItem) && cJSON_GetArraySize(posItem) == 3) {',
        '                for (int j = 0; j < 3; ++j)',
        '                    ((float*)&obj->pos)[j] = (float)cJSON_GetArrayItem(posItem, j)->valuedouble;',
        '            }',
        '',
        '            cJSON* rotItem = cJSON_GetObjectItem(objJson, "rot");',
        '            if (cJSON_IsArray(rotItem) && cJSON_GetArraySize(rotItem) == 4) {',
        '                for (int j = 0; j < 4; ++j)',
        '                    ((float*)&obj->rot)[j] = (float)cJSON_GetArrayItem(rotItem, j)->valuedouble;',
        '            }',
        '',
        '            cJSON* scaleItem = cJSON_GetObjectItem(objJson, "scale");',
        '            if (cJSON_IsArray(scaleItem) && cJSON_GetArraySize(scaleItem) == 3) {',
        '                for (int j = 0; j < 3; ++j)',
        '                    ((float*)&obj->scale)[j] = (float)cJSON_GetArrayItem(scaleItem, j)->valuedouble;',
        '            }',
        '',
        '            cJSON* objectTypeItem = cJSON_GetObjectItem(objJson, "objectType");',
        '            if (cJSON_IsNumber(objectTypeItem)) {',
        '                obj->objectType = (ObjectType)objectTypeItem->valueint;',
        '            } else if (cJSON_IsString(objectTypeItem)) {',
        '                const char* typeName = objectTypeItem->valuestring;',
        '                int found = -1;',
        '                for (int idx = 0; idx < OBJECT_COUNT; idx++) {',
        '                    if (strcmp(typeName, g_objectTypeNames[idx]) == 0) {',
        '                        found = idx;',
        '                        break;',
        '                    }',
        '                }',
        '                if (found != -1) obj->objectType = (ObjectType)found;',
        '                else {',
        '                    obj->objectType = OBJECT_PRIMITIVE;',
        '                    fprintf(stderr, "Unknown object type \\"%s\\", defaulting to Primitive\\n", typeName);',
        '                }',
        '            }',
        '',
        '            cJSON* pipelineItem = cJSON_GetObjectItem(objJson, "pipeline");',
        '            if (cJSON_IsNumber(pipelineItem)) {',
        '                obj->pipeline = (RenderPipeline)pipelineItem->valueint;',
        '            } else if (cJSON_IsString(pipelineItem)) {',
        '                const char* pipeName = pipelineItem->valuestring;',
        '                int found = -1;',
        '                for (int idx = 0; idx < RENDER_COUNT; idx++) {',
        '                    if (strcmp(pipeName, g_renderPipelineNames[idx]) == 0) {',
        '                        found = idx;',
        '                        break;',
        '                    }',
        '                }',
        '                if (found != -1) obj->pipeline = (RenderPipeline)found;',
        '                else {',
        '                    obj->pipeline = RENDER_DEFAULT;',
        '                    fprintf(stderr, "Unknown pipeline \\"%s\\", defaulting to Default\\n", pipeName);',
        '                }',
        '            }',
        '',
        '            // Type‑specific data',
        '            switch (obj->objectType) {',
    ])

    # Generate parsing cases for each variant
    for variant_name, fields in variants.items():
        enum_name = f"OBJECT_{variant_name.upper()}"
        lines.append(f'                case {enum_name}: {{')
        parse_lines = parse_variant_object(variant_name, fields, f"obj->data.{variant_name}")
        for l in parse_lines:
            lines.append('    ' + l)
        lines.append(f'                    break;')
        lines.append(f'                }}')

    lines.extend([
        '                default:',
        '                    break;',
        '            }',
        '        }',
        '        // Update objectCount if array was present',
        '        if (arraySize > 0) scene->objectCount = arraySize;',
        '    }',
        '',
        '    cJSON_Delete(root);',
        '    return 1;',
        '}'
    ])

    output_c.parent.mkdir(parents=True, exist_ok=True)
    with open(output_c, 'w', encoding='utf-8') as f:
        f.write(header + '\n' + '\n'.join(lines))

    common.log_success(f"Generated {output_c}")
    return True

if __name__ == '__main__':
    generate_scene_json(Path("src/scene_data.h"), Path("src/generated/scene_json.cpp"))