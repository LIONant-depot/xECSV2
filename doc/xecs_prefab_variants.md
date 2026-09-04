<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Prefabs](xecs_prefab.md) / Variants

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

A variant also know as hierarchical Prefabs or Recursive Prefabs, is in simple terms an Prefab which contains prefab instances. So this means that in order to create an prefab instance it must go throw the entire process of creating prefab instances recursively. To speed up this process xECS have all prefabs including variants instantiated as entities but invisible thank to the xecs::prefab::tag. This allows for fast instance creation at runtime. In the editor side changing a child prefab will have recursive consequences. 

