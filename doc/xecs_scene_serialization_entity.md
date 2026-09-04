<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Serialization](xecs_scene_serialization.md) / Entity

Serialization of a Scene mainly deals with serializing entities. Scenes knows which entities they need to save because they have a list of those entities. This is the same list that is used to map entity references. This document will talk about the common blocks when serializing the different types of entities.

There are 3 kinds of entities in a scene, and each of those entities can have different sections:

* **Share Entities** - Entities that are share across all scenes and constructed by the system
  * [Raw - Entities](xecs_scene_serialization_entity_raw.md)
* **Local Entities** - Entities which can not be reference by entities outside this scene
  * [Raw - Entities](xecs_scene_serialization_entity_raw.md)
  * [Prefab Instances - Entities](xecs_scene_serialization_entity_prefab_instances.md)
* **Global Entities** - Entities which can be reference by any entity
  * [Ranges](xecs_scene_serialization_ranges.md)
  * [Raw - Entities](xecs_scene_serialization_entity_raw.md)
  * [Prefab Instances - Entities](xecs_scene_serialization_entity_prefab_instances.md)
## Entities Sections

We have a block to indicate that we are going to be dealing with entities. The contents of the block will specify what are the specifics. This helps document the file as well as to start new sections.

~~~cpp
[ Entities ]
{ Name:s             Type:s    Count:d }
//-----------------  --------  -------
  "local-entities"   "raw"        5
~~~

|||
|:------------------:|-------------|
| Name               | Is the name of the section. Usually is either "local-entities", "global-entities" or "share-entities"    |
| SerializationMode  | When serializing components of a particular type there are 3 kinds of modes that we support.             |
| Type               | Indicates which kind of entities there are in this section usually either "raw" or "prefabs Instance"    |
| Count              | This is context sensitive, for raw entities this means how many archetypes will have, but for "prefab instance" it means how many entities we have. |

Below this block you will find the following sections depending of the type of entity:

* [Raw - Entities](xecs_scene_serialization_entity_raw.md) - These types of entities are just typical case.
* [Prefab Instances - Entities](xecs_scene_serialization_entity_prefab_instances.md) - These are entities that have been created from an Prefab or a Prefab Variant.

## Share Entities 

There are some technical details that are worth knowing about the way share entities are serialize.

<< [Look here to learn more](xecs_scene_serialization_entity_shared.md) >>



---
