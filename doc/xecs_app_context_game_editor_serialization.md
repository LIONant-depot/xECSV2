<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / [Game Editor](xecs_app_context_game_editor.md) / Serialization

The organization of the saved project is as follows:

Folder Architecture:

* [Scene File Format](xecs_scene_serialization.md)
* [Prefab File Format](xecs_scene_serialization_entity.md)
* [Ranges File Format](xecs_scene_ranges_serialization.md)

~~~cpp
+- Project Files
|   +- Settings
|   |   |- Project.ranges       // Ranges File
|   |
|   +- Editor Scene Assets      // Editor Scenes
|   |   +- Scenes 
|   |   |   +- GUID.scene       // Scene Folder
|   |   |   |    |- GUID.layer  // Layer file
|   |   |   |    |- GUID.layer  // Layer file
|   |   |   |
|   |   |   +- GUID.scene       // Scene Folder
|   |   |        |- GUID.layer  // Layer file
|   |   |        |- GUID.layer  // Layer file
|   |
|   +- Game Scene Assets        // Game Scenes
|   |   +- Scenes               // All the scenes
|   |   |   +- GUID.scene       // Scene Folder
|   |   |   |    |- GUID.layer  // Layer file
|   |   |   |    |- GUID.layer  // Layer file
|   |   |   |
|   |   |   +- GUID.scene       // Scene Folder
|   |   |        |- GUID.layer  // Layer file
|   |   |        |- GUID.layer  // Layer file
|   |   |
|   |   +- Prefabs              // All the prefabs
|   |       |- GUID.prefab      // Prefab File
|   |       |- GUID.prefab      // Prefab Files
|   |
~~~
