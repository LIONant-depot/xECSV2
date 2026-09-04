<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Prefabs

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Prefabs also known in the game industry as (blue-prints, or archetypes) is a way to to define an Entity or Scene with a specific set of values in them for the shake of cloning them (know as prefabs instances) at runtime or in the editor. There are two kind of prefabs:

1. [Entity Prefabs](xecs_prefab_entity.md) - Which means we are dealing with a single entity
2. [Scene Prefabs](xecs_prefab_scene.md) - Which means that we are dealing with a group of entities (where there is not root entity)

If an entity has children is it a Entity Prefab or a Scene Prefab?<br>
This are still consider Scene Prefabs because there is more than one. However here the Scene prefab Pivot may be removed in favor of the single parent, since both of them would represent exactly the same case.

There are two Context in which prefabs operate:

1. **Runtime** - The function calls will be made by systems. 
2. **Editor** - The user will be able to create prefabs and prefabs instance using the editor. This context provides extra functionality over the Runtime such property overrides and component overrides. 

Check more details on [Prefab Editor](xecs_editor_prefab.md)

## Prefab GUID vs EntityID

The prefab GUID **(xecs::prefab::guid)** is the official Identifier of a Prefab (not matter if it is an entity prefab or a scene prefab). This global identifier will also be used as its resource GUID. The Prefab EntityID **(xecs::component::entity)** is the temporary ID of a prefab. Please note that every time a prefab is loaded the EntityID will be different, the GUID in the other hand is persistent. This is why it is discourage to use the entity id for prefabs.

~~~cpp
constexpr static auto prefab_rctype_v = xcore::guid::rctype<>{ xcore::guid::plugin<>("xECS/Prefab"), "Prefab" };
using                 guid            = xcore::guid::rcfull_singletype<prefab_rctype_v>;
~~~

The runtime will have a hash-map that will map between the xecs::prefab::guid to the actual EntityID. 

Prefabs are consider assets and when you save them they will have assets names. However all assets must be converted to resources. It is the job of the Game Editor to serialize a prefab asset into a resource. Ones it has become a resource it will follow the resource naming convention: **(resource_instance_GUID.resource_type_GUID)**. This means that the file name will be hard to read.


## Serializing Prefabs

Serializing prefabs is very similar to how a [scene serializes](xecs_scene_serialization.md). Scene groups entities base on where they were created, usually in the editor under the context of a particular scene. Prefabs don't belong to any scene, prefab instances do. Prefabs are thought to be entities that are globally accessible by any scene and always loaded, they are in the truer sense global objects. To know which entity is a prefab it uses a [exclusive_tag](xecs_component_type_tag.md) named ***xecs::prefab::tag***.

## Creating a Prefab Instance

When creating a prefab instance you must have the **xecs::prefab::guid** for the prefab where the instance will be based on. If the prefab has children all the children will also be instantiated. Note that in the function when creating the instance you can add/remove and initialize any component you like. Please note that any reference to any other entity won't be updated right away. 

For more info check out [Entity Prefabs](xecs_prefab_entity.md) and [Scene Prefabs](xecs_prefab_scene.md) as they talked about the specifics for each case.

---