"""Build the MKI Roads double street light mesh from the vanilla street light (#400).

Run with Blender 4.2 or later (`npm run road-pack -- meshes` does this):

    blender -b --factory-startup --python street_light_double.py -- <StreetLight_static.glb> <out.fbx> <meta.json> <armTrimCm>

The input is the vanilla `StreetLight_static` exported from the game paks (LOD0 glTF, read-only, kept under .local and
never committed). The vanilla light (UE cm, the arm along +Y) is one mesh of loose parts: a pole and a box collar rising
to Z 780 that dog-legs to a post at Y -99.5; the arm root block (Z 894-940) on that post with the arm and lamp head along
+Y; and the power hook-up (an insulator at Y -173.5) on its own bracket and housing with a cable loop down to the
dog-leg. The maintainer: one centre hook-up in line with the pole and two arms with lights on either side (#400). So:

- the dog-leg, the post, the cable loop, the insulator housing and its bracket are left out;
- the collar's straight section is stretched up to the underside of the arm root block;
- the arm root block, arm and lamp head move over the pole, are cut at the pole's centre line, and are mirrored;
- the thin outer arm is shortened by <armTrimCm> along its own pitch (the maintainer: 2.8 m, lamps over the lanes);
- the insulator sits on top of the hub in line with the pole.

The pole's axis becomes the origin. Material slots are renamed to the vanilla slot names, in the vanilla order
(Paint, Color, Normal); the import assigns the vanilla materials to them by asset path. Convex UCX_ hulls (pole, collar,
hub, each arm tube, each lamp head) are the collision. <meta.json> records the power connection and lamp positions the
manifest must match.

Blender coordinates are metres with Blender y = -UE Y (the glTF import and the FBX export each mirror one axis).
"""
import json
import math
import sys

import bpy
import bmesh  # after bpy: outside Blender (the bpy module) bmesh exists only once bpy is loaded
from mathutils import Vector

ARGS = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT_FBX, OUT_META, ARM_TRIM_CM = ARGS[0], ARGS[1], ARGS[2], float(ARGS[3])
MESH_NAME = "SM_MKIR_StreetLightDouble"

# Vanilla geometry (UE cm), measured on StreetLight_static (CUE4Parse, #400).
POLE_AXIS_Y = -2.5           # centre line of the pole and collar
POST_AXIS_Y = -99.5          # the arm root block's pivot post
INSULATOR_AXIS_Y = -173.5    # the insulator
COLLAR_STRAIGHT_TOP_Z = 775.0
COLLAR_STRETCH_FROM_Z = 700.0
HUB_UNDERSIDE_Z = 906.0      # the arm root block's underside at the pole
HUB_TOP_Z = 940.0
INSULATOR_BASE_Z = 896.0
THIN_ARM_START_Y = 300.0     # beyond the arm tube: the thin arm's outer end and the lamp head
VANILLA_SPOT = (637.3, 997.7)        # Deco_StreetLight's pooled spot light (Y, Z), pitched straight down
VANILLA_SHAFT = (637.3, 1047.99)     # and its light shaft
VANILLA_POWER_Z = 949.41             # FGPowerConnection at (0, -173.32, 949.41)
SLOTS = [("Paint", "StreetLight_Inst"), ("Color", "DecalColor_Masked"), ("Normal", "Decal_Normal")]

for item in list(bpy.data.objects):
    bpy.data.objects.remove(item)
bpy.ops.import_scene.gltf(filepath=SRC)
source = next(item for item in bpy.data.objects if item.type == "MESH")
mesh = source.data
for index in range(len(mesh.uv_layers) - 1, 0, -1):   # the exporter writes 8 UV sets; only the first is the game's
    mesh.uv_layers.remove(mesh.uv_layers[index])

# Vanilla slot names and order.
by_name = {material.name.split(".")[0]: index for index, material in enumerate(mesh.materials)}
order = [by_name[vanilla] for _, vanilla in SLOTS]
remap = {old: new for new, old in enumerate(order)}
indices = [remap[polygon.material_index] for polygon in mesh.polygons]
materials = [mesh.materials[index] for index in order]
mesh.materials.clear()   # resets every polygon's material index to 0, so the remap is applied after it
for (slot, _), material in zip(SLOTS, materials):
    material.name = slot
    mesh.materials.append(material)
mesh.polygons.foreach_set("material_index", indices)
mesh.update()

# Loose parts, classified by their UE bounds.
bm = bmesh.new()
bm.from_mesh(mesh)
GROUPS = ["pole", "collar", "arm", "insulator", "drop"]
layer = bm.verts.layers.int.new("mkir_part")   # before any vertex reference is taken: adding a layer invalidates them
bm.verts.ensure_lookup_table()
seen, parts = set(), []
for start in bm.verts:
    if start.index in seen:
        continue
    seen.add(start.index)
    stack, part = [start], []
    while stack:
        vert = stack.pop()
        part.append(vert)
        for edge in vert.link_edges:
            other = edge.other_vert(vert)
            if other.index not in seen:
                seen.add(other.index)
                stack.append(other)
    ys = [-v.co.y * 100 for v in part]
    zs = [v.co.z * 100 for v in part]
    y0, y1, z0, z1 = min(ys), max(ys), min(zs), max(zs)
    if y1 <= -140 and z0 >= 895.5:
        group = "insulator"
    elif z0 >= 893:
        group = "arm"
    elif y0 < -120 and z0 >= 740:
        group = "drop"       # cable loop, insulator housing, the hook-up's bracket and their decals
    elif z0 >= 780:
        group = "drop"       # the dog-leg's top and the pivot post
    elif z0 >= 660 and z1 > 682:
        group = "collar"
    else:
        group = "pole"
    parts.append((group, part))
counts = {}
for group, _ in parts:
    counts[group] = counts.get(group, 0) + 1
print("street light parts:", counts)
if counts != {"pole": 87, "collar": 34, "arm": 23, "insulator": 16, "drop": 55}:
    raise RuntimeError("unexpected vanilla street light parts %s; the game mesh changed, re-measure it" % counts)

for group, part in parts:
    for vert in part:
        vert[layer] = GROUPS.index(group)
group_of = lambda vert: GROUPS[vert[layer]]
bmesh.ops.delete(bm, geom=[v for v in bm.verts if group_of(v) == "drop"], context="VERTS")


def ue(v):
    return -v.co.y * 100, v.co.z * 100


def set_ue(v, y, z):
    v.co.y, v.co.z = -y / 100, z / 100


# The collar's dog-leg starts above its straight section: cut it off there and close the top.
collar_geom = [v for v in bm.verts if group_of(v) == "collar"]
collar_faces = list({f for v in collar_geom for f in v.link_faces})
collar_edges = list({e for v in collar_geom for e in v.link_edges})
cut = bmesh.ops.bisect_plane(bm, geom=collar_geom + collar_edges + collar_faces, plane_co=Vector((0, 0, COLLAR_STRAIGHT_TOP_Z / 100)), plane_no=Vector((0, 0, 1)), clear_outer=True)
bmesh.ops.holes_fill(bm, edges=[e for e in cut["geom_cut"] if isinstance(e, bmesh.types.BMEdge) and e.is_valid], sides=0)

# The thin arm's pitch, from its end rings.
arm_verts = [v for v in bm.verts if group_of(v) == "arm"]
inner = [ue(v) for v in arm_verts if 284 <= ue(v)[0] <= 300 and ue(v)[1] > 980]
outer = [ue(v) for v in arm_verts if 570 <= ue(v)[0] <= 586 and 1040 < ue(v)[1] < 1080]
centre = lambda points: (sum(p[0] for p in points) / len(points), sum(p[1] for p in points) / len(points))
(ay, az), (by, bz) = centre(inner), centre(outer)
pitch = math.atan2(bz - az, by - ay)
trim_y, trim_z = ARM_TRIM_CM * math.cos(pitch), ARM_TRIM_CM * math.sin(pitch)

for v in bm.verts:
    group = group_of(v)
    y, z = ue(v)
    if group == "collar" and z > COLLAR_STRETCH_FROM_Z:
        z = COLLAR_STRETCH_FROM_Z + (z - COLLAR_STRETCH_FROM_Z) * (HUB_UNDERSIDE_Z - COLLAR_STRETCH_FROM_Z) / (COLLAR_STRAIGHT_TOP_Z - COLLAR_STRETCH_FROM_Z)
    elif group == "arm":
        if y > THIN_ARM_START_Y:
            y, z = y - trim_y, z - trim_z
        y += POLE_AXIS_Y - POST_AXIS_Y
    elif group == "insulator":
        y, z = y + POLE_AXIS_Y - INSULATOR_AXIS_Y, z + HUB_TOP_Z - INSULATOR_BASE_Z
    set_ue(v, y - POLE_AXIS_Y, z)

# Mirror the arm assembly about the pole's centre line: keep its arm side, then add the mirror image.
arm_set = [v for v in bm.verts if group_of(v) == "arm"]
arm_faces = list({f for v in arm_set for f in v.link_faces})
arm_edges = list({e for v in arm_set for e in v.link_edges})
cut = bmesh.ops.bisect_plane(bm, geom=arm_set + arm_edges + arm_faces, plane_co=Vector((0, 0, 0)), plane_no=Vector((0, 1, 0)), clear_outer=True)
half = list({f for f in arm_faces if f.is_valid} | {f for e in cut["geom_cut"] if isinstance(e, bmesh.types.BMEdge) for f in e.link_faces})
duplicate = bmesh.ops.duplicate(bm, geom=half)
new_faces = [item for item in duplicate["geom"] if isinstance(item, bmesh.types.BMFace)]
for vert in [item for item in duplicate["geom"] if isinstance(item, bmesh.types.BMVert)]:
    vert.co.y = -vert.co.y
bmesh.ops.reverse_faces(bm, faces=new_faces)
seam = [v for v in bm.verts if abs(v.co.y) < 1e-5 and group_of(v) == "arm"]
bmesh.ops.remove_doubles(bm, verts=seam, dist=1e-4)
bm.verts.layers.int.remove(layer)
bm.to_mesh(mesh)
bm.free()
mesh.update()
source.name = MESH_NAME
mesh.name = MESH_NAME

# Normals from the edited geometry (the glTF's custom normals do not follow the mirror).
if mesh.has_custom_normals:
    with bpy.context.temp_override(object=source, active_object=source, selected_objects=[source]):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
for polygon in mesh.polygons:
    polygon.use_smooth = True
if hasattr(mesh, "set_sharp_from_angle"):
    mesh.set_sharp_from_angle(angle=math.radians(50))

# Collision: convex hulls of the pole, the collar, the hub, and each arm's tube and lamp head.
lamp_y = VANILLA_SPOT[0] + POLE_AXIS_Y - POST_AXIS_Y - trim_y - POLE_AXIS_Y
points = [(-v.co.y * 100, v.co.z * 100, v.co.x * 100, v.co) for v in mesh.vertices]
hull_regions = {
    "Pole": lambda y, z: abs(y) < 40 and z < 668,
    "Collar": lambda y, z: abs(y) < 40 and 668 <= z < HUB_UNDERSIDE_Z,
    "Hub": lambda y, z: abs(y) < 135 and HUB_UNDERSIDE_Z - 15 <= z <= HUB_TOP_Z + 2,
    "ArmR": lambda y, z: 120 <= y < lamp_y - 70 and z > HUB_UNDERSIDE_Z - 15,
    "ArmL": lambda y, z: -lamp_y + 70 < y <= -120 and z > HUB_UNDERSIDE_Z - 15,
    "HeadR": lambda y, z: y >= lamp_y - 70 and z > HUB_UNDERSIDE_Z,
    "HeadL": lambda y, z: y <= -lamp_y + 70 and z > HUB_UNDERSIDE_Z,
}
hulls = []
for index, (name, inside) in enumerate(hull_regions.items()):
    region = [co.copy() for y, z, _, co in points if inside(y, z)]
    if len(region) < 4:
        raise RuntimeError("collision hull %s has %d points" % (name, len(region)))
    hull_bm = bmesh.new()
    for co in region:
        hull_bm.verts.new(co)
    result = bmesh.ops.convex_hull(hull_bm, input=hull_bm.verts[:])
    bmesh.ops.delete(hull_bm, geom=list({item for item in result["geom_interior"] + result["geom_unused"] if isinstance(item, bmesh.types.BMVert)}), context="VERTS")
    hull_mesh = bpy.data.meshes.new("UCX_%s_%02d" % (MESH_NAME, index))
    hull_bm.to_mesh(hull_mesh)
    hull_bm.free()
    hull = bpy.data.objects.new(hull_mesh.name, hull_mesh)
    bpy.context.scene.collection.objects.link(hull)
    hulls.append(hull)

meta = {
    "armTrimCm": ARM_TRIM_CM,
    "powerConnectionCm": [0, 0, round(VANILLA_POWER_Z + HUB_TOP_Z - INSULATOR_BASE_Z, 1)],
    "lamps": [
        {"spotLightCm": [0, round(side * lamp_y, 1), round(VANILLA_SPOT[1] - trim_z, 1)],
         "lightShaftCm": [0, round(side * lamp_y, 1), round(VANILLA_SHAFT[1] - trim_z, 1)], "yawDeg": 0 if side > 0 else 180}
        for side in (1, -1)
    ],
    "triangles": sum(len(p.vertices) - 2 for p in mesh.polygons),
    "slotTriangles": {slot: sum(len(p.vertices) - 2 for p in mesh.polygons if p.material_index == index) for index, (slot, _) in enumerate(SLOTS)},
    "collisionHulls": len(hulls),
    "boundsCm": {
        "min": [round(min(p[2] for p in points), 1), round(min(p[0] for p in points), 1), round(min(p[1] for p in points), 1)],
        "max": [round(max(p[2] for p in points), 1), round(max(p[0] for p in points), 1), round(max(p[1] for p in points), 1)],
    },
}
with open(OUT_META, "w", encoding="utf-8") as handle:
    json.dump(meta, handle, indent=2)
    handle.write("\n")

bpy.ops.object.select_all(action="DESELECT")
for item in [source] + hulls:
    item.select_set(True)
bpy.context.view_layer.objects.active = source
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_UNITS", mesh_smooth_type="FACE",
    use_mesh_modifiers=True, add_leaf_bones=False, bake_anim=False, use_custom_props=False, path_mode="STRIP",
)
print(json.dumps(meta))
