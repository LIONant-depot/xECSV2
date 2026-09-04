<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /> <br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / [Game Editor](xecs_app_context_game_editor.md) / Prefab

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Please check the concept of [Editor Entities](xecs_editor_entities.md) Before moving forward and as core to what we will talk about here.

There are a few things that the user can editor when it comes down to Prefabs. First is the actual [prefab](xecs_prefab_md) or a [variant](xecs_prefab_variant.md), then there are the [Entity prefab instance](xecs_prefab_entity.md), or the [Scene prefab instance](xecs_prefab_scene.md), all those elements are core to what the Prefab Editor is suppose to deal with. To make matters more confusing the Prefab Editor is kind of merge inside the Scene Editor to provide a more fluid experience for the user.

## Editing a prefab

The problems that we need to solve are:

1. Update the prefab 
2. Update all the prefab instances.
3. Update Relevant Scene Prefabs.
4. Update Scene Prefabs instances.
5. Update the prefab Variants (original prefab, other variants and the relevant Scene prefabs, and their variants).
6. Update the all the Variant instances correctly.

### CASE 1: The simplest way possible to edit a prefab would be:

1. Serialize the scene
2. Create an instance of the prefab.
3. Allow the user to modify anything it feel compel to do.
4. Serialize/replace the updated prefab to the disk
5. Reload the entire scene with all the prefabs

However doing so would be a bit slow. So there are a few things that we can do to improve things. 
### CASE 2: Create a version would allow to no serialize the entire scene and to only deal with the relevant data. so lets revise the steps:

1. Create an instance of the prefab.
2. Allow the user to modify anything it feel compel to do.
3. Identify all the prefab instances, relevant Scene Prefabs, and all possible variants (basically variants that used this prefabs) instances
4. Serialize all those instances to a temporary file
5. Delete those instances and the relevant prefab/scene/variants
6. Delete the actual relevant prefab/scene/variants
7. Reload all the key prefab/scene/variants again  
8. Reload the temporary file with all the instances

Now this still sounds overkill for many typical operations such changing an integer value in some component. Specially all the serialization work that seems that we are doing. 
### CASE 3: So lets again review the steps:

1. Allow the user to edit the prefab itself
2. Any changes made in the prefab will automatically cascade to the regular instances. 
3. If there are scene prefabs with instances or the original prefab or variants those instances will also update, and then all instances of those prefabs. Like that recursively until it was all done.

This last version is also a bit scary because changing a single integer one time may take a while before all the changes has cascade in the entire scene. 

TODO: EXPLORE MORE CASES...




## Instance Component

To keep track of which entities are prefab instances we have a component that the editor uses. It is able to track all the necessary changes for any instances on the editor side.

~~~cpp
struct editor_instance_tracker
{
    constexpr static auto typedef_v = xecs::component::type::data
    {
        .m_pName            = "EditorPrafabInstanceTracker"
    };

    struct component
    {
        enum class type : std::uint8_t
        { OVERRIDES         // This component was part of the original archetype and I am overriding some properties
        , NEW               // New component so all properties are assumed to be overwritten 
        , REMOVE            // I removed this component from the original archetype
        };

        struct override
        {
            std::string             m_PropertyName;
            xcore::crc<32>          m_PropertyType;
        };

        xecs::component::type::guid m_ComponentTypeGuid;
        std::vector<override>       m_PropertyOverrides;
        type                        m_Type;
    };

    xecs::prefab::guid              m_Guid;                     // Guid of the prefab
    std::vector<component>          m_lComponents;
};
~~~

Cloning prefabs is call creating a prefab-instance. In the [editor context Prefab-instances](xecs_editor_entities.md) are linked with the original Prefabs, and carry an extra component.

| State | Description |
|-------|----------|
| Added Component (X) | Added a new component to the prefab-instance. This component can be anything. This also  means that when the instance was created the component did not exited in the prefab. All properties for this component are assumed to be overriden. |
| Deleted Component (X) | Deleted a component from the prefab-instance. This component can be anything. This also means that when the instance was created the component did exist in the prefab. |
| Override Property (X,V) | Means that the component modified a value(V) of property (X). |

