#!/usr/bin/env python3
import sys
from pathlib import Path
import common   # your existing common.py

def generate_scene_json(input_h: Path, output_c: Path) -> bool:
    common.log_info(f"Reading input file: {input_h}")
    with open(input_h, 'r', encoding='utf-8') as f:
        content = f.read()
    common.log_info(f"File content length: {len(content)}")
    common.log_info(f"First 200 chars: {repr(content[:200])}")

    if not input_h.exists():
        common.log_error(f"Input not found: {input_h}")
        return False

    with open(input_h) as f:
        content = f.read()

    # Parse the Scene struct
    fields = common.parse_struct_fields(content, "Scene")
    if not fields:
        common.log_error("Could not parse Scene struct")
        return False

    # Parse the nested SceneObject struct
    obj_fields = common.parse_struct_fields(content, "SceneObject")
    if not obj_fields:
        common.log_error("Could not parse SceneObject struct")
        return False

    # Build output C code
    header = common.make_header("meta_scene_json.py")
    lines = [
        '#include <cJSON.h>',
        '#include "../scene_data.h"',
        '#include "mesh_data.h"',
        '#include <string.h>',
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
    ]

    # Add each field of SceneObject
    for typ, name, is_array, arr_size, is_ptr in obj_fields:
        if is_array and typ.startswith('char'):
            lines.append(f'        cJSON_AddStringToObject(objJson, "{name}", obj->{name});')
        else:
            if typ == 'DirectX::XMFLOAT3':
                arr_var = f"{name}Arr"
                lines.extend([
                    f'        cJSON* {arr_var} = cJSON_CreateFloatArray((float*)&obj->{name}, 3);',
                    f'        cJSON_AddItemToObject(objJson, "{name}", {arr_var});'
                ])
            elif typ == 'DirectX::XMFLOAT4':
                arr_var = f"{name}Arr"
                lines.extend([
                    f'        cJSON* {arr_var} = cJSON_CreateFloatArray((float*)&obj->{name}, 4);',
                    f'        cJSON_AddItemToObject(objJson, "{name}", {arr_var});'
                ])
            elif typ == 'PrimitiveType':
                lines.append(f'        cJSON_AddNumberToObject(objJson, "{name}", obj->{name});')
            else:
                # fallback – treat as number (int/float)
                lines.append(f'        cJSON_AddNumberToObject(objJson, "{name}", obj->{name});')

    lines.extend([
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
    ])

    # Read each field back
    for typ, name, is_array, arr_size, is_ptr in obj_fields:
        if is_array and typ.startswith('char'):
            lines.append(f'            cJSON* {name}Item = cJSON_GetObjectItem(objJson, "{name}");')
            lines.append(f'            if (cJSON_IsString({name}Item)) strncpy(obj->{name}, {name}Item->valuestring, sizeof(obj->{name})-1);')
        else:
            lines.append(f'            cJSON* {name}Item = cJSON_GetObjectItem(objJson, "{name}");')
            if typ == 'DirectX::XMFLOAT3':
                lines.extend([
                    f'            if (cJSON_IsArray({name}Item) && cJSON_GetArraySize({name}Item) == 3) {{',
                    f'                for (int j = 0; j < 3; ++j)',
                    f'                    ((float*)&obj->{name})[j] = (float)cJSON_GetArrayItem({name}Item, j)->valuedouble;',
                    f'            }}',
                ])
            elif typ == 'DirectX::XMFLOAT4':
                lines.extend([
                    f'            if (cJSON_IsArray({name}Item) && cJSON_GetArraySize({name}Item) == 4) {{',
                    f'                for (int j = 0; j < 4; ++j)',
                    f'                    ((float*)&obj->{name})[j] = (float)cJSON_GetArrayItem({name}Item, j)->valuedouble;',
                    f'            }}',
                ])
            elif typ == 'PrimitiveType':
                lines.append(f'            if (cJSON_IsNumber({name}Item)) obj->{name} = (PrimitiveType){name}Item->valueint;')
            else:
                lines.append(f'            if (cJSON_IsNumber({name}Item)) obj->{name} = {name}Item->valuedouble;')

    lines.extend([
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