<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Component](xecs_component.md) / Entity

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

The Entity represents the concept of an object, and this objects are the atomic unit of xECS. Unlike C++ Object Oriented Objects which are identified by their type and instance pointer, and Entity does not have a type and stead of a pointer it contains a Unique Identifier. You should never keep a pointer to an entity or to any of its components. because their data can be moved at any moment. If you need a reference to an entity gets keep a copy of its entity-component.

Base on entity dependencies you could break them in 3 types:

* **Local-Entities -** Entities that can not have references across different scenes.
* **Global-Entities -** Entities that can be refer by any other entity independently of which scene you are. Please note that even if you have a reference to the global entity does not mean you can get access it because the scene may not be loaded.
* **Share-Entities -** Entities that are share across all scenes. This entities are not created by the user rather this entities are created by the system and their job is to keep the share-components.

To learn more about [Entity in Scenes](xecs_scene_serialization_entity.md)

## Entity Component

The entity is an actual component with a foot print of 64bits, That 64 bits is an ID with 2 parts:
1. An Index to its global_info, which is the first part of the entity unique id.
2. A Validation structure, which contains the other part of the unique id, and a flag 

|||
|-------------|--------------------------------------|
| c++ type    | xecs::component::entity              |
| Header file | xecs_component.h                     | 
~~~cpp
union entity final                              // 64 bits size
{
    std::uint64_t       m_Value;                // Representation as a raw 64bits (This is actually the unique ID with an extra flag from the validation structure)
    struct                                      // This structure is 32bits
    {
        std::uint32_t   m_GlobalInfoIndex;      // Index to the global_info
        validation      m_Validation;           // Validation structure
    };
};
~~~

## Entity Validation
There is a large array of global_info structures, the index of an entry represents part of the entities unique ID. The actual global_info structure contains: 
1. Pointer to its pool so it knows how to locate its components 
2. An index in the pool where all its components are
3. A Validation structure which should match the component::entity Validation

|||
|-------------|--------------------------------------|
| c++ type    | xecs::component::entity::validation  |
| Header file | xecs_component.h                     | 

~~~cpp
union validation final                          // 32 bits size
{
    std::uint32_t       m_Value;                // Representation as a raw 32bits
    struct
    {
        std::uint32_t   m_Generation:31         // Used as a counter to avoid collision with older entity ids
        ,               m_bZombie:1;            // Indicates if the entity is a zombie (dead but not removed)
    };
};
~~~

Every entity in the game will have an entity component. The contents of the entity component is never changed by the user. It is assume to be private and should never be access directly. Most system use the entity component whenever they want to add or remove component, delete the particular entity, etc. So more like an ID that a real component.

## Entity global_info

The global_info is consider a detail for most users in xECS and really no one needs to care about it directly. As a concept is good to know as when doing more complex stuff (such global-entities) you should know the reasons why there are some limitations, however most people won't care.

There is an array of global_info this array is virtual because it uses virtual memory so it can be very large (up to 2^32 entries). However the actual break down in really is about ***~140 million entities*** reserved for local-entities for the runtime part, and the rest is for global-entities and their ranges. So xECS should be able to handle a large amount of entities. 

Global entities are a fascinated subject which You can learn more about [global_info ranges](xecs_scene_serialization_ranges.md)
|||
|-------------|--------------------------------------|
| c++ type    | xecs::component::entity::global_info |
| Header file | xecs_component.h                     | 

~~~cpp
struct global_info final                                    // 16 bytes Size (Assuming 64bits build)
{
    xecs::pool::instance*           m_pPool         {};     // Pointer to the pool contains the entity components
    xecs::pool::index               m_PoolIndex     {};     // Index in the array of the pool where the actual components are
    validation                      m_Validation    {};     // The validation structure which should match with the entity component
};
~~~

## Creating Entities

Entities can be created in a variety of ways:

* Directly created by scripts through the GameMgr or by a System.
* Directly created by Instantiating an Entity-Prefab or a Scene-Prefab.
* Indirectly loading Scenes.
* Indirectly created by using share-components
* Directly created by using the editor

When instantiating entities is faster to create them at ones rather than one by one. The secret of an ECS is to group and executed.

---