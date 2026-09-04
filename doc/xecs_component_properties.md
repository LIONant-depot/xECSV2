<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Component](xecs_component.md) / Properties

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>
* [Component Serialization](xecs_component_typedef_serialization.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

One of the core functionalities of components is to have properties, without them the component won't be able to participate in many of the features of xECS such Prefabs, Serialization, etc. So it should be consider a core feature of a food ECS. 

The library that xECS uses to solve the properties is call Properties.

| Properties by LIONant | Details |
|:----:|---|
| <img src="https://i.imgur.com/GfJb3sQ.jpg" width="120px" /> | * [Project Depot](https://gitlab.com/LIONant/properties)<br>* [Documentation](https://gitlab.com/LIONant/properties/blob/master/docs/Documentation.md)<br>* [Examples](https://gitlab.com/LIONant/properties/-/blob/master/src/Examples/Examples.h)<br>* Imgui Support<br> * MIT

The first limitation that you will encounter is the types that the properties support. This can be extended but it should always be kept small. Here are the supported types:

| C++ Supported Types     | Comment |
|:-----------------------:|---------|
| int32_t                 | signed integer number |
| uint32_t                | unsigned integer number |
| float                   | signed floating point number. |
| double                  | double precision floating point number. |
| xecs::component::entity | This is to be use whenever there is a reference to another entity |
| xcore::vector2          | vector2 provided by xcore |
| xcore::vector3          | vector3 provided by xcore, (SIMD optimized with 4*sizeof(float)) |
| xcore::vector3d         | vector3d provided by xcore, Not SIMD optimize but of 3*sizeof(float) |
| xcore::radian3          | angles provided by xcore, Roll Pitch Yaw, in radians |
| xcore::quaternion       | rotations provided by xcore, (SIMD optimized) |
| xcore::cstring          | Strings |

The property system is able to handle C++ Hierarchical base class properties, and by type only base properties. It is up to the user to decide what they would like to use however that Hierarchical version requires a virtual function, while the by type does not. So we will encourage to use the by type for the 99.9% of the cases.

Example of giving properties to a component, Please note that there is not need to give any data to the typedef_v. Some limitations:
* Properties must be located in a global namespace
* Type base properties they should also be located next to the component type
~~~cpp
struct transform
{
    static constexpr auto typedef_v = xecs::component::type::data{};

    xcore::quaternion   m_Rotation;
    xcore::vector3      m_Position;
    xcore::vector3      m_Scale;
};

// Here are the properties for our transform component
property_begin( transform )
{
      property_var( m_Position )
    , property_var( m_Rotation  )
    , property_var( m_Scale  )
} property_end_h()
~~~

## Uses of Properties by xECS

Properties are used for for the following:

1. Copy Components
2. Editing of individual properties (Muti-select and Single-select)
3. Documentation
4. Debugging
5. Serialization 
6. Prefab (Entity and Scene based)
7. Scripting
8. General Reflection
9. etc


## Supporting containers

The property system support different kinds of containers:

* Array
* Vector
* Custom

Here is an example on how the property system deals with vectors
~~~cpp
struct hierarchy
{
    static constexpr auto typedef_v = xecs::component::type::data{};

    xecs::component::entity              m_Parent;          // Who is our parent entity?
    std::vector<xecs::component::entity> m_Children;        // A vector with all our children entities 
};

// Here are the properties for our transform component
property_begin( hierarchy )
{
      property_var( m_Parent )
    , property_var( m_Children  )
} property_end_h()
~~~

## Supporting C++ Pointers

TODO:

---
