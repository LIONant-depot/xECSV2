<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Prefabs](xecs_prefab.md) / Scene

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Please make sure that [Entity Prefabs](xecs_prefab_entity.md) make sense before understanding what this is. Scene Prefabs are similar in concept to an [Entity Prefab](xecs_prefab_entity.md) except the entity has children. When you are dealing with many entities in away you are creating a mini-Scene. All Scene Prefabs must have a single root entity, this entity will be the pivot point for their children as expected. This Root Entity has the option to be automatically delete it when you create instances of the ScenePrefab.

The Scene Prefab can have both regular entities and Prefab Instances (which include both Scene Prefab Instances and Entity Prefab Instances). Unlike real scenes they must have the pivot point entity and they can not have global entities. 

## The Root Entity

The Root Entity serves as the pivot point so it must have a transform component. The user can choose if ones it creates the instance the pivot point is retained as a parent entity or not. The default is that is won't because having hierarchies creates complexities and slows down performance. 

## Creating an instance

So creation of a ScenePrefab instance is done with the following steps:
1. Clones the Root Entity (with add or removed component as requested by the user)
2. Clones the rest of the hierarchy 
3. Resolves entity references inside the prefab instances, such parent with children etc
4. Calls the Root Entity Callback 
5. Computes all the L2W information for the entire hierarchy 
6. Removes the RootEntity (if the user requested) also updated all the direct children by removing the parent component.

Scene Prefabs are allowed to have references to other entities inside the Scene Prefab as well as global references. One thing to note is that share components are not allowed to have references to Scene Prefabs only to global references. This was decided as an optimization, since it is a very rare case and adds more complexity. 
