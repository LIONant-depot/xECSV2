<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / [Game Editor](xecs_app_context_game_editor.md) / [Component](xecs_app_context_game_editor.md) / Transform

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

The transform component as a quire component for all entities. However just because it has a transform component it does not mean that the entity will have a position at runtime. This component is Editor side component and is there to determine which positional components the user should have at runtime.

~~~cpp
struct xecs::editor::component::transform
{
    enum force_keep
    { ROTATION    = (1<<0)                      // If static and unused we still want to keep the rotation
    , SCALE       = (1<<1)                      // If static and unused we still want to keep the scale
    };

    xcore::vector3          m_Position;         // Editor side position for entities that don't have a position at all
    xcore::radian3          m_Rotation;         // Remember the users preferences on how to rotate the object

    editor*                 m_pEditorInterface; // So when the properties tries to get or set can reach the
                                                // actual runtime data

    std::uint8_t            m_ForceKeep:2       {ROTATION|SCALE};
    bool                    m_isStatic:1        {}
    ,                       m_is2D:1            {}
    ,                       m_isPosDouble:1     {}
    ,                       m_isUnitScale:1     {}
    ,                       m_DeleteTransform:1 {};
}
~~~

Based on the options selected in the transform component a minimum set of runtime components will be created:

~~~cpp

namespace world
{
    // When doing doubles the position is located in a separate component
    struct transform
    {
        xcore::matrix4          m_L2W;      // Local to World
    }

    // extra position component for world position with doubles
    struct position_3d_dbl
    {
        xcore::dvector3d        m_Value;
    };
}

namespace local
{
    // When doing doubles the position is located in a separate component
    struct transform
    {
        xcore::matrix4          m_L2P;      // Local to Parent
    }

    struct position_3d_dbl
    {
        xcore::dvector3d        m_Value;
    };

    struct position_3d
    {
        xcore::vector3d         m_Value;
    };

    struct position_2d
    {
        xcore::vector2          m_Value;
    };

    struct rotation_3d
    {
        xcore::quaternion       m_Value;
    };

    struct rotation_2d
    {
        float                   m_Value;
    };

    struct scale_3d
    {
        xcore::vector3          m_Value;
    };

    struct scale_2d
    {
        xcore::vector2          m_Value;
    };

    struct scale_1d
    {
        float                   m_Value;
    };
}

~~~

If an Entity is mark as static and all its parents are also static the optimizer should be able to extract it from the hierarchy with the final world::transform, other wise if it has a dynamic parent then it must remain as part of the hierarchy. 

There will be a systems that load the value from the runtime to the transform and vice versa. 

| SELF    | PARENT | world::transform  | local::transform | local::position  | local::scale    | local::rotation |
|:-------:|:------:|:-----------------:|:----------------:|:----------------:|:---------------:|:---------------:|
| STATIC  |    N   |        Y          |        N         |         N        |        N        |        N        |
| STATIC  |    Y   |        Y          |        Y         |         N        |        N        |        N        |
| DYNAMIC |    N   |        Y          |        N         |         Y?       |        Y?       |        Y?       |
| DYNAMIC |    Y   |        Y          |        Y         |         Y?       |        Y?       |        Y?       |


## Transforms

The standard transform is as follows:

> Entity->local::transform = Matrix4.setup( Entity->local::scale, Entity->local::rotation, Entity->local::position );

> Entity->world::transform = ParentEntity->world::transform * Entity->local::transform

When doing some animations may change to:

> Entity->animation::transform = AnimationKeyFrameMatrix * BindPoseMatrix-1

> Entity->local::transform = Matrix4.setup( Entity->local::scale, Entity->local::rotation, Entity->local::position ) * <br>Entity->animation::transform

> Entity->world::transform = ParentEntity->world::transform * Entity->local::transform

## Systems used to transform

In order of execution

| MustHave                               | Must Not Have           | At least one of                   | Description |
|:--------------------------------------:|:-----------------------:|:---------------------------------:|-------------|
| world::transform                       | local::transform<br>local::position<br>local::rotation<br>local::scale         | NONE                              | static Entities with static parents or not parents (NO SYSTEM IS REQUIRE)  |
| world::transform | local::transform  <br>Parent   <br>Children   | local::position<br>local::rotation<br>local::scale             | dynamic Entities without parents or children. Systems must build the world::transform  |
| world::transform  <br>Children | Parent              | local::position<br>local::rotation<br>local::scale                         | Root Entities. Systems must build the world::transform and if the entity is Dynamic the local::transform, and recursively for all its children |

