#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Create the minimal UESplatting demo map and import the generated PLY fixture."""

from __future__ import annotations

from pathlib import Path

import unreal


REPO_ROOT = Path(__file__).resolve().parents[1]
CONTENT_ROOT = "/Game/UESplattingDemo"
MAP_PATH = f"{CONTENT_ROOT}/Maps/UESplattingDemo"
SPLAT_PATH = REPO_ROOT / "samples" / "Data" / "UESplatting_Demo.ply"


def load_class(path: str) -> unreal.Class:
    result = unreal.load_class(None, path)
    if result is None:
        raise RuntimeError(f"Could not load class: {path}")
    return result


def spawn(actor_subsystem, class_path: str, label: str, location, rotation=None):
    actor_class = load_class(class_path)
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class,
        location,
        rotation or unreal.Rotator(),
        transient=False,
    )
    if actor is None:
        raise RuntimeError(f"Could not spawn {class_path}")
    actor.set_actor_label(label)
    return actor


def import_demo_splat() -> unreal.Object:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(SPLAT_PATH))
    task.set_editor_property("destination_path", f"{CONTENT_ROOT}/Splats")
    task.set_editor_property("destination_name", "UESplatting_Demo")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported_paths = list(task.get_editor_property("imported_object_paths"))
    if not imported_paths:
        raise RuntimeError(f"UESplatting did not import fixture: {SPLAT_PATH}")
    asset = unreal.load_asset(imported_paths[0])
    if asset is None:
        raise RuntimeError(f"Could not load imported splat: {imported_paths[0]}")

    # The checked-in demo asset must not serialize this machine's import path.
    asset.set_reimport_source_file_path("")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save imported splat: {imported_paths[0]}")
    return asset


def create_demo_map(splat_asset: unreal.Object) -> None:
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not level_subsystem.load_level(MAP_PATH):
            raise RuntimeError(f"Could not load level: {MAP_PATH}")
        for actor in actor_subsystem.get_all_level_actors():
            actor_subsystem.destroy_actor(actor)
    elif not level_subsystem.new_level(MAP_PATH):
        raise RuntimeError(f"Could not create level: {MAP_PATH}")

    floor = spawn(
        actor_subsystem,
        "/Script/Engine.StaticMeshActor",
        "Demo Floor",
        unreal.Vector(0.0, 0.0, -5.0),
    )
    floor_component = floor.get_editor_property("static_mesh_component")
    floor_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    floor_component.set_material(0, unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"))
    floor.set_actor_scale3d(unreal.Vector(200.0, 200.0, 0.1))

    splat_actor = spawn(
        actor_subsystem,
        "/Script/UESplatting.GaussianSplatActor",
        "UESplatting Demo Splat",
        unreal.Vector(0.0, 0.0, 0.0),
    )
    splat_actor.set_splat_asset(splat_asset)

    spawn(
        actor_subsystem,
        "/Script/UESplattingDemo.UESplattingDemoRuntimeSmokeActor",
        "Runtime Loading Smoke (opt-in)",
        unreal.Vector(0.0, 0.0, 0.0),
    )

    directional = spawn(
        actor_subsystem,
        "/Script/Engine.DirectionalLight",
        "Key Light",
        unreal.Vector(),
        unreal.Rotator(-38.0, -42.0, 0.0),
    )
    directional.get_editor_property("directional_light_component").set_editor_property("intensity", 5.0)

    sky_light = spawn(
        actor_subsystem,
        "/Script/Engine.SkyLight",
        "Sky Light",
        unreal.Vector(0.0, 0.0, 250.0),
    )
    sky_light.get_editor_property("light_component").set_editor_property("intensity", 0.7)

    spawn(
        actor_subsystem,
        "/Script/Engine.SkyAtmosphere",
        "Sky Atmosphere",
        unreal.Vector(),
    )

    camera_location = unreal.Vector(-350.0, -350.0, 195.0)
    camera_target = unreal.Vector(0.0, 0.0, 125.0)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, camera_target)
    camera = spawn(
        actor_subsystem,
        "/Script/Engine.CameraActor",
        "Demo Camera",
        camera_location,
        camera_rotation,
    )
    camera.get_editor_property("camera_component").set_editor_property("field_of_view", 52.0)

    spawn(
        actor_subsystem,
        "/Script/Engine.PlayerStart",
        "Player Start",
        camera_location,
        camera_rotation,
    )

    if not level_subsystem.save_current_level():
        raise RuntimeError("Could not save UESplatting demo level")
    unreal.EditorAssetLibrary.save_directory(CONTENT_ROOT, only_if_is_dirty=False, recursive=True)


def main() -> None:
    if not SPLAT_PATH.is_file():
        raise RuntimeError(
            f"Missing {SPLAT_PATH}. Run Scripts/generate_demo_splat.py first."
        )

    splat_asset = import_demo_splat()
    create_demo_map(splat_asset)
    unreal.log(f"UESplatting demo content created at {MAP_PATH}")


main()
