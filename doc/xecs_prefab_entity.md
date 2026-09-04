<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Prefabs](xecs_prefab.md) / Entity

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Entity Prefab is a Predefined Entity that contain a set of components with a specific set of values in them. One this Prefab Entity is defined the user can create clones of them in the editor call Entity Prefab Instances. These instances will be unique entities in their own right and the game is able to run any system on them. While the Original Entity Prefab has an exclusive tag **(xecs::prefab::tag)** which exclude it from most queries, and is left as a resource to create instances from it.

In the context of the game, Entity Prefab Instances have not additional information from any other entity. This is desirable because then these entities can share the same Archetype as any regular entity with the same components. 

In the context of the Editor, Entity Prefab Instances like any other entity from a Scene will have an additional component that will link back the to the Editor.DLL Entities. From there it will know if it is a prefab instance or not so it will know how to serialize properly.

Note that Entity Prefabs are not allowed to have children. If you add a child into a Entity Prefab you have convert it into a [Scene Prefab](xecs_prefab_scene.md)

## Entity Prefab vs Entity Prefab Instance

The Entity Prefab is an entity which is used to create the Entity Prefab Instances. Entity Prefabs are actual a kind of resource that must be loaded by the system. Like all resource they are assumed to be global entities so they don't belong to any Scene. 

Entity Prefab Instances are just like any other type of entity, in fact in the Game.DLL after it loads them or create them it won't know any difference at all. This allows entities to group together into larger Archetypes and be more efficient.

## Creating an instance

Creation of Entity Prefab Instances are much faster than [Scene Prefabs](xecs_prefab_scene.md) because of the lower complexity that they deal with. The creation of a instance is done with the following steps:
1. Clones the entity (with add or removed component as requested by the user)
2. Calls the Callback 

Note that there are not resolve references here. Entity Prefabs are not allow to have references to any other entity except for global references.

---
