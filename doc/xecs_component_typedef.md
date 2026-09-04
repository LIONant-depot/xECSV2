<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Component](xecs_component.md) / typedef_v

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_typedef_serialization.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

typedef_v is short for Type Definition Value, and what it means is that we are defining the particular type of component as well as all the details about how the component should behave in the system. It is a variable rather than been inherited or something else because it frees the components to have their own hierarchy if they choose to do so, and because it is a static variable and is constructed at compile time there is not wasted memory or performance for that matter. The system uses the typedef_v to build the **xecs::component::type::info** for each component. 



---


