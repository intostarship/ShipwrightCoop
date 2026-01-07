#!/usr/bin/env python3
"""
Generate actor serialization info from source code.
Parses actor headers and source files to extract:
- Struct layout (SkelAnime offset, jointTable size, actionFunc offset)
- Action function names for each actor
- Field types and offsets for safe serialization

Usage: python3 generate_actor_sync.py > ActorSyncGenerated.inc
"""

import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple

# Paths
BASE_DIR = Path(__file__).parent.parent.parent.parent.parent
ACTORS_DIR = BASE_DIR / "src" / "overlays" / "actors"
ACTOR_TABLE = BASE_DIR / "include" / "tables" / "actor_table.h"

@dataclass
class FieldInfo:
    name: str
    offset: int
    type_str: str
    size: int = 0
    is_pointer: bool = False
    is_action_func: bool = False
    is_syncable: bool = False  # Safe to sync (primitive, no pointers)
    array_size: int = 1
    c_type: str = ""  # Cleaned C type for code generation

@dataclass
class ActorInfo:
    name: str  # e.g., "EnMd"
    actor_id: str  # e.g., "ACTOR_EN_MD"
    struct_size: int = 0
    skel_anime_offset: int = 0
    skel_anime_field: str = ""  # Actual field name (may be skelAnime, skelanime, etc.)
    action_func_offset: int = 0
    action_func_field: str = ""  # Actual field name
    joint_table_offset: int = 0  # Actual offset of jointTable in struct
    joint_table_field: str = ""  # Actual field name
    joint_table_size: int = 0
    morph_table_offset: int = 0  # Actual offset of morphTable in struct
    morph_table_field: str = ""  # Actual field name
    morph_table_size: int = 0
    action_funcs: List[str] = field(default_factory=list)
    fields: List[FieldInfo] = field(default_factory=list)
    syncable_fields: List[FieldInfo] = field(default_factory=list)  # Fields safe to sync after actionFunc
    has_skel_anime: bool = False
    header_path: str = ""  # Actual path to header file (for includes)

def parse_offset(comment: str) -> Optional[int]:
    """Extract hex offset from comment like /* 0x014C */"""
    match = re.search(r'/\*\s*0x([0-9A-Fa-f]+)\s*\*/', comment)
    if match:
        return int(match.group(1), 16)
    return None

def parse_array_size(type_str: str) -> Tuple[str, int]:
    """Parse array size from type like Vec3s[17] -> (Vec3s, 17)"""
    match = re.match(r'(\w+)\[(\d+)\]', type_str)
    if match:
        return match.group(1), int(match.group(2))
    match = re.match(r'(\w+)\[(\w+)\]', type_str)
    if match:
        # Named constant like STALFOS_LIMB_MAX
        return match.group(1), -1  # Will need to resolve later
    return type_str, 1

def get_type_size(type_str: str) -> int:
    """Get size of a type in bytes"""
    sizes = {
        'u8': 1, 's8': 1, 'char': 1,
        'u16': 2, 's16': 2,
        'u32': 4, 's32': 4, 'f32': 4, 'int': 4,
        'u64': 8, 's64': 8, 'f64': 8,
        'Vec3f': 12, 'Vec3s': 6, 'Vec3i': 12,
        'SkelAnime': 0x44,  # Size of SkelAnime struct
        'Actor': 0x14C,  # Base Actor size (may vary)
        'ColliderCylinder': 0x4C,
        'ColliderQuad': 0x80,
        'NpcInteractInfo': 0x28,
        'BodyBreak': 0x18,
    }
    base_type, array_size = parse_array_size(type_str)
    base_size = sizes.get(base_type, 4)  # Default to 4 (pointer size)
    if array_size > 0:
        return base_size * array_size
    return base_size

def is_pointer_type(type_str: str) -> bool:
    """Check if type is a pointer"""
    return '*' in type_str or 'Func' in type_str

# Types that contain pointers and should NOT be synced
UNSAFE_TYPES = {
    'Collider', 'ColliderCylinder', 'ColliderQuad', 'ColliderJntSph',
    'ColliderTris', 'ColliderSphere',
    'SkelAnime',  # Contains animation pointers
    'NpcInteractInfo',  # Contains pointers
    'BodyBreak',  # Contains pointers
    'Actor',  # Base actor (synced separately)
    'LightNode', 'LightInfo',
    'DynaPolyActor',
    'Path',  # Pointer to path data
}

# Primitive types that are safe to sync
PRIMITIVE_TYPES = {
    'u8', 's8', 'char',
    'u16', 's16',
    'u32', 's32', 'int',
    'u64', 's64',
    'f32', 'f64',
}

# Struct types that are safe to sync (no pointers)
SAFE_STRUCT_TYPES = {
    'Vec3f', 'Vec3s', 'Vec3i',
    'Vec2f', 'Vec2s',
    'Color_RGB8', 'Color_RGBA8',
    'PosRot',
}

def is_syncable_type(type_str: str) -> bool:
    """Check if a type is safe to sync (no pointers)"""
    # Remove array notation
    base_type = re.sub(r'\[\w+\]', '', type_str).strip()

    # Check for explicit pointer
    if '*' in base_type or 'Func' in base_type:
        return False

    # Check against unsafe types
    for unsafe in UNSAFE_TYPES:
        if unsafe in base_type:
            return False

    # Check if it's a primitive
    if base_type in PRIMITIVE_TYPES:
        return True

    # Check if it's a safe struct
    if base_type in SAFE_STRUCT_TYPES:
        return True

    # Unknown types - default to unsafe to be safe
    return False

def get_c_type_info(type_str: str) -> Tuple[str, int, str]:
    """Get clean C type, array size, and JSON serialization type"""
    # Parse array notation
    array_match = re.search(r'(\w+)\[(\d+)\]', type_str)
    if array_match:
        base_type = array_match.group(1)
        array_size = int(array_match.group(2))
    else:
        base_type = type_str.strip()
        array_size = 1

    # Determine JSON type for serialization
    if base_type in ('u8', 's8', 'char'):
        json_type = 'uint8'
    elif base_type in ('u16', 's16'):
        json_type = 'int16'
    elif base_type in ('u32', 's32', 'int'):
        json_type = 'int32'
    elif base_type in ('f32',):
        json_type = 'float'
    elif base_type in ('Vec3f',):
        json_type = 'vec3f'
    elif base_type in ('Vec3s',):
        json_type = 'vec3s'
    else:
        json_type = 'unknown'

    return base_type, array_size, json_type

def parse_actor_table() -> Dict[str, str]:
    """Parse actor_table.h to map folder names to ACTOR_* enum names"""
    folder_to_enum = {}
    try:
        content = ACTOR_TABLE.read_text()
        # Pattern: DEFINE_ACTOR(En_Test, ACTOR_EN_TEST, ...)
        pattern = re.compile(r'DEFINE_ACTOR(?:_INTERNAL)?\s*\(\s*(\w+)\s*,\s*(ACTOR_\w+)\s*,')
        for match in pattern.finditer(content):
            folder_name = match.group(1)  # e.g., "En_Test"
            actor_enum = match.group(2)   # e.g., "ACTOR_EN_TEST"
            # Convert to folder format: En_Test -> ovl_En_Test
            folder_to_enum[f"ovl_{folder_name}"] = actor_enum
    except Exception as e:
        print(f"Warning: Could not parse actor table: {e}", file=sys.stderr)
    return folder_to_enum

def parse_header(header_path: Path) -> Optional[ActorInfo]:
    """Parse an actor header file to extract struct info"""
    try:
        content = header_path.read_text()
    except:
        return None

    # Parse enum values for array size resolution
    # Pattern: /* 0x3D */ STALFOS_LIMB_MAX
    enum_values = {}
    enum_pattern = re.compile(r'/\*\s*(?:0x)?([0-9A-Fa-f]+)\s*\*/\s*(\w+)')
    for match in enum_pattern.finditer(content):
        value = int(match.group(1), 16)
        name = match.group(2)
        enum_values[name] = value

    # Find struct name from typedef
    # Pattern: typedef void (*EnMdActionFunc)(struct EnMd*, PlayState*);
    action_func_match = re.search(r'typedef void \(\*(\w+ActionFunc)\)', content)

    # Find main struct definition
    # Pattern: typedef struct EnMd { ... } EnMd;
    struct_match = re.search(r'typedef struct (\w+)\s*\{([^}]+)\}\s*(\w+)\s*;', content, re.DOTALL)
    if not struct_match:
        return None

    struct_name = struct_match.group(1)
    struct_body = struct_match.group(2)

    # Convert struct name to ACTOR_ID format
    # EnMd -> ACTOR_EN_MD, EnTest -> ACTOR_EN_TEST
    actor_id = "ACTOR_" + re.sub(r'([a-z])([A-Z])', r'\1_\2', struct_name).upper()

    actor_info = ActorInfo(name=struct_name, actor_id=actor_id)

    # Parse fields
    # Pattern: /* 0x014C */ SkelAnime skelAnime;
    field_pattern = re.compile(r'/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*(\w+(?:\[\w+\])?(?:\s*\*)?\s*)\s+(\w+)(?:\[(\w+)\])?;')

    for match in field_pattern.finditer(struct_body):
        offset = int(match.group(1), 16)
        type_str = match.group(2).strip()
        field_name = match.group(3)
        array_spec = match.group(4)

        if array_spec:
            type_str = f"{type_str}[{array_spec}]"

        field_info = FieldInfo(
            name=field_name,
            offset=offset,
            type_str=type_str,
            is_pointer=is_pointer_type(type_str),
            is_action_func='ActionFunc' in type_str
        )

        # Track special fields (only first occurrence) - store actual field names!
        if (field_name.lower() == 'skelanime' or type_str == 'SkelAnime') and not actor_info.has_skel_anime:
            actor_info.skel_anime_offset = offset
            actor_info.skel_anime_field = field_name  # Store actual field name
            actor_info.has_skel_anime = True
        elif field_name.lower() == 'jointtable' and actor_info.joint_table_size == 0:
            actor_info.joint_table_offset = offset
            actor_info.joint_table_field = field_name  # Store actual field name
            base, size = parse_array_size(type_str)
            if size < 0:
                # Try to resolve named constant
                match = re.search(r'\[(\w+)\]', type_str)
                if match and match.group(1) in enum_values:
                    size = enum_values[match.group(1)]
            actor_info.joint_table_size = size if size > 0 else 20  # Default
        elif field_name.lower() == 'morphtable' and actor_info.morph_table_size == 0:
            actor_info.morph_table_offset = offset
            actor_info.morph_table_field = field_name  # Store actual field name
            base, size = parse_array_size(type_str)
            if size < 0:
                # Try to resolve named constant
                match = re.search(r'\[(\w+)\]', type_str)
                if match and match.group(1) in enum_values:
                    size = enum_values[match.group(1)]
            actor_info.morph_table_size = size if size > 0 else 20
        elif field_info.is_action_func and actor_info.action_func_offset == 0:
            actor_info.action_func_offset = offset
            actor_info.action_func_field = field_name  # Store actual field name

        actor_info.fields.append(field_info)

    # Get struct size from comment like "// size = 0x0324"
    size_match = re.search(r'//\s*size\s*=\s*0x([0-9A-Fa-f]+)', content)
    if size_match:
        actor_info.struct_size = int(size_match.group(1), 16)

    # Identify syncable fields AFTER actionFunc (state variables)
    # Skip jointTable and morphTable as they're handled separately
    if actor_info.action_func_offset > 0:
        for field_info in actor_info.fields:
            # Must be after actionFunc
            if field_info.offset <= actor_info.action_func_offset:
                continue
            # Skip jointTable and morphTable (handled separately)
            if field_info.name in ('jointTable', 'morphTable'):
                continue
            # Skip pointers and action funcs
            if field_info.is_pointer or field_info.is_action_func:
                continue
            # Check if type is safe to sync
            if is_syncable_type(field_info.type_str):
                base_type, array_size, json_type = get_c_type_info(field_info.type_str)
                field_info.is_syncable = True
                field_info.c_type = base_type
                field_info.array_size = array_size
                actor_info.syncable_fields.append(field_info)

    return actor_info

def parse_source(source_path: Path, actor_info: ActorInfo) -> None:
    """Parse an actor source file to extract action functions"""
    try:
        content = source_path.read_text()
    except:
        return

    # Find action function assignments
    # Pattern: EnTest_SetupAction(this, EnTest_WaitGround);
    # Or: this->actionFunc = EnTest_WaitGround;
    setup_pattern = re.compile(rf'{actor_info.name}_SetupAction\s*\(\s*this\s*,\s*(\w+)\s*\)')
    direct_pattern = re.compile(rf'this->actionFunc\s*=\s*(\w+)\s*;')

    action_funcs = set()

    # Function names to skip (not real action functions)
    skip_names = {'actionFunc', 'NULL', 'func', 'Function', 'Callback', 'arg1', 'arg0'}
    # Prefixes that indicate macros, not real functions
    skip_prefixes = ('SKJ_ACTION_',)

    for match in setup_pattern.finditer(content):
        func_name = match.group(1)
        if func_name not in skip_names and not any(func_name.startswith(p) for p in skip_prefixes):
            action_funcs.add(func_name)

    for match in direct_pattern.finditer(content):
        func_name = match.group(1)
        if func_name not in skip_names and not any(func_name.startswith(p) for p in skip_prefixes):
            action_funcs.add(func_name)

    actor_info.action_funcs = sorted(list(action_funcs))

def scan_actors() -> List[ActorInfo]:
    """Scan all actor directories and extract info"""
    actors = []

    if not ACTORS_DIR.exists():
        print(f"Error: Actors directory not found: {ACTORS_DIR}", file=sys.stderr)
        return actors

    # Get folder->enum mapping from actor_table.h
    folder_to_enum = parse_actor_table()
    print(f"Loaded {len(folder_to_enum)} actor mappings from actor_table.h", file=sys.stderr)

    for actor_dir in sorted(ACTORS_DIR.iterdir()):
        if not actor_dir.is_dir():
            continue

        folder_name = actor_dir.name  # e.g., "ovl_En_Md"

        # Find header and source files
        header_files = list(actor_dir.glob("*.h"))
        source_files = list(actor_dir.glob("*.c"))

        if not header_files:
            continue

        # Parse header
        actor_info = parse_header(header_files[0])
        if actor_info is None or not actor_info.has_skel_anime:
            continue  # Skip actors without SkelAnime (not syncable)

        # Store the actual header path for includes (relative to src/)
        header_rel = header_files[0].relative_to(BASE_DIR / "src")
        actor_info.header_path = str(header_rel)

        # Get correct ACTOR_* enum from actor_table.h
        if folder_name in folder_to_enum:
            actor_info.actor_id = folder_to_enum[folder_name]
        else:
            print(f"Warning: No actor table entry for {folder_name}", file=sys.stderr)
            continue  # Skip actors not in table

        # Parse source for action functions
        if source_files:
            parse_source(source_files[0], actor_info)

        if actor_info.action_funcs:  # Only include actors with action functions
            actors.append(actor_info)

    return actors

def generate_c_code(actors: List[ActorInfo]) -> str:
    """Generate C code for actor sync registry"""
    lines = []
    lines.append("// Auto-generated by generate_actor_sync.py")
    lines.append("// DO NOT EDIT MANUALLY - regenerate with: python3 tools/generate_actor_sync.py")
    lines.append("//")
    lines.append("// All offsets are computed at COMPILE-TIME using offsetof() - no runtime guessing!")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <stddef.h>")
    lines.append("#include <stdint.h>")
    lines.append("")

    # Forward declare action functions FIRST with extern "C"
    # This establishes C linkage before headers potentially re-declare them
    lines.append("// Forward declarations for action functions (defined in C files)")
    lines.append("extern \"C\" {")

    # Forward declare all actor structs first (needed for function signatures)
    for actor in actors:
        lines.append(f"struct {actor.name};")
    lines.append("")

    # Forward declare all action functions
    for actor in actors:
        for func in actor.action_funcs:
            lines.append(f"void {func}({actor.name}*, PlayState*);")
    lines.append("}")
    lines.append("")

    # Include actor headers for offsetof() compile-time resolution
    # Headers have been fixed for C++ compatibility (this->thisx, guard for duplicates)
    lines.append("// Include actor headers for offsetof() compile-time resolution")
    for actor in actors:
        lines.append(f'#include "{actor.header_path}"')
    lines.append("")

    # Function tables
    lines.append("// Action function tables")
    for actor in actors:
        if actor.action_funcs:
            lines.append(f"static void* {actor.name}_ActionFuncs[] = {{")
            for func in actor.action_funcs:
                lines.append(f"    (void*){func},")
            lines.append("    NULL  // Terminator")
            lines.append("};")
            lines.append("")

    # Field type enum
    lines.append("// Field types for serialization")
    lines.append("typedef enum {")
    lines.append("    FIELD_TYPE_U8,")
    lines.append("    FIELD_TYPE_S8,")
    lines.append("    FIELD_TYPE_U16,")
    lines.append("    FIELD_TYPE_S16,")
    lines.append("    FIELD_TYPE_U32,")
    lines.append("    FIELD_TYPE_S32,")
    lines.append("    FIELD_TYPE_F32,")
    lines.append("    FIELD_TYPE_VEC3F,")
    lines.append("    FIELD_TYPE_VEC3S,")
    lines.append("} FieldType;")
    lines.append("")

    # Field descriptor struct
    lines.append("// Field descriptor for state variables")
    lines.append("typedef struct {")
    lines.append("    const char* name;")
    lines.append("    size_t offset;")
    lines.append("    FieldType type;")
    lines.append("    u8 arraySize;  // 1 for non-arrays")
    lines.append("} FieldDescriptor;")
    lines.append("")

    # Generate field descriptor arrays for each actor
    lines.append("// Field descriptors for syncable state variables (offsets via offsetof())")
    for actor in actors:
        if actor.syncable_fields:
            lines.append(f"static FieldDescriptor {actor.name}_Fields[] = {{")
            for field in actor.syncable_fields:
                # Map C type to FieldType enum
                type_map = {
                    'u8': 'FIELD_TYPE_U8', 's8': 'FIELD_TYPE_S8', 'char': 'FIELD_TYPE_S8',
                    'u16': 'FIELD_TYPE_U16', 's16': 'FIELD_TYPE_S16',
                    'u32': 'FIELD_TYPE_U32', 's32': 'FIELD_TYPE_S32', 'int': 'FIELD_TYPE_S32',
                    'f32': 'FIELD_TYPE_F32',
                    'Vec3f': 'FIELD_TYPE_VEC3F', 'Vec3s': 'FIELD_TYPE_VEC3S',
                }
                field_type = type_map.get(field.c_type, 'FIELD_TYPE_U8')
                # Use offsetof() for compile-time resolution - 100% accurate!
                lines.append(f'    {{ "{field.name}", offsetof({actor.name}, {field.name}), {field_type}, {field.array_size} }},')
            lines.append("    { NULL, 0, FIELD_TYPE_U8, 0 }  // Terminator")
            lines.append("};")
            lines.append("")

    # Actor sync info struct (updated with field descriptors)
    lines.append("// Actor sync info")
    lines.append("typedef struct {")
    lines.append("    s16 actorId;")
    lines.append("    size_t structSize;")
    lines.append("    size_t skelAnimeOffset;")
    lines.append("    size_t actionFuncOffset;")
    lines.append("    size_t jointTableOffset;  // Actual offset of jointTable (NOT after SkelAnime!)")
    lines.append("    u8 jointCount;")
    lines.append("    size_t morphTableOffset;  // Actual offset of morphTable")
    lines.append("    u8 morphCount;")
    lines.append("    void** actionFuncTable;")
    lines.append("    u8 actionFuncCount;")
    lines.append("    FieldDescriptor* stateFields;  // Syncable state fields after actionFunc")
    lines.append("    u8 stateFieldCount;")
    lines.append("} ActorSyncInfo;")
    lines.append("")

    # Registry - ALL offsets via offsetof(), sizes via sizeof() - 100% compile-time!
    lines.append("// Actor sync registry - ALL offsets computed at compile-time via offsetof()")
    lines.append("static ActorSyncInfo actorSyncRegistry[] = {")
    for actor in actors:
        func_count = len(actor.action_funcs)
        func_table = f"{actor.name}_ActionFuncs" if actor.action_funcs else "NULL"
        state_fields = f"{actor.name}_Fields" if actor.syncable_fields else "NULL"
        state_count = len(actor.syncable_fields)

        # Use sizeof() for struct size
        size_expr = f"sizeof({actor.name})"

        # Use offsetof() with actual field names - compile-time resolution!
        if actor.skel_anime_field:
            skel_offset = f"offsetof({actor.name}, {actor.skel_anime_field})"
        else:
            skel_offset = "0"

        if actor.action_func_field:
            action_offset = f"offsetof({actor.name}, {actor.action_func_field})"
        else:
            action_offset = "0"

        # jointTable and morphTable - use actual field names
        if actor.joint_table_field:
            joint_offset = f"offsetof({actor.name}, {actor.joint_table_field})"
        else:
            joint_offset = "0"

        if actor.morph_table_field:
            morph_offset = f"offsetof({actor.name}, {actor.morph_table_field})"
        else:
            morph_offset = "0"

        lines.append(f"    {{ {actor.actor_id}, {size_expr}, {skel_offset}, "
                    f"{action_offset}, {joint_offset}, {actor.joint_table_size}, "
                    f"{morph_offset}, {actor.morph_table_size}, "
                    f"{func_table}, {func_count}, {state_fields}, {state_count} }},")
    lines.append("    { -1, 0, 0, 0, 0, 0, 0, 0, NULL, 0, NULL, 0 }  // Terminator")
    lines.append("};")
    lines.append("")

    # Lookup function
    lines.append("static ActorSyncInfo* GetActorSyncInfo(s16 actorId) {")
    lines.append("    for (int i = 0; actorSyncRegistry[i].actorId >= 0; i++) {")
    lines.append("        if (actorSyncRegistry[i].actorId == actorId) {")
    lines.append("            return &actorSyncRegistry[i];")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return NULL;")
    lines.append("}")
    lines.append("")

    # Action function index lookup
    lines.append("static int GetActionFuncIndex(ActorSyncInfo* info, void* funcPtr) {")
    lines.append("    if (info == NULL || info->actionFuncTable == NULL) return -1;")
    lines.append("    for (int i = 0; i < info->actionFuncCount; i++) {")
    lines.append("        if (info->actionFuncTable[i] == funcPtr) return i;")
    lines.append("    }")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")

    lines.append("static void* GetActionFuncByIndex(ActorSyncInfo* info, int index) {")
    lines.append("    if (info == NULL || info->actionFuncTable == NULL) return NULL;")
    lines.append("    if (index < 0 || index >= info->actionFuncCount) return NULL;")
    lines.append("    return info->actionFuncTable[index];")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)

def main():
    print(f"Scanning actors in: {ACTORS_DIR}", file=sys.stderr)
    actors = scan_actors()
    print(f"Found {len(actors)} syncable actors with action functions", file=sys.stderr)

    # Count totals
    total_funcs = sum(len(a.action_funcs) for a in actors)
    total_fields = sum(len(a.syncable_fields) for a in actors)
    print(f"Total: {total_funcs} action functions, {total_fields} syncable state fields", file=sys.stderr)
    print("", file=sys.stderr)

    # Print summary to stderr
    for actor in actors:
        field_names = [f.name for f in actor.syncable_fields]
        fields_str = ", ".join(field_names[:5])
        if len(field_names) > 5:
            fields_str += f", ... (+{len(field_names)-5} more)"
        print(f"  {actor.name}: funcs={len(actor.action_funcs)}, "
              f"stateFields={len(actor.syncable_fields)} [{fields_str}]", file=sys.stderr)

    # Generate C code to stdout
    print(generate_c_code(actors))

if __name__ == "__main__":
    main()
