# Rules
- Y is up.
- Default DX12 winding order.

# Style rules:
- global variables have prefix 'g_*'
- member variables have prefix 'm_*'
- enums are accessed by namespace first e.g.: 'PrimitiveType::PRIMITIVE_CUBE'

# STL rules
- Attempt to avoid STL usage as much as possible, but std::vector is permitted (for now) if you absolutely cannot use a flat array, and std::unordered_map is permitted if you need to have key-value pair hash table.