<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /> <br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / References

Entity Global Info (*global-infos* in short) is a data structure in the component manager that allows to have references to the actual entities that are located in pools in some archetype. These global entities are also what allow us to keep a unique identifier for entities. In fact the value inside the ***xecs::component::entity.m_GlobalIndex*** is literally an index to one of these global entities. 

There are a few types of scenes and depending of the type of scene you may have different types of problems with Global entities. There is a document that talk about the [different scenes here](xecs_scene_instance.md). But basically the way to think about a scene is that is a spawner of entities. When a local-scene is spawn all its entities ids need to be re-mapped into the runtime-global-info ranges. See more about these [ranges in this document](). Global-Scenes they will have reserved global-info Index Ranges (*ranges*) those ranges are directly mapped into the virtual space of the global-info.

### Concepts to keep in mind

Some of these concepts are not guaranteed to make it to the final design but important to not forger neither.

|  Concept              | Comments |
|:---------------------:|----------|
| Long term storage     | Entities which the IDs are going to be save to this at some point. |
| Short term storage    | Entities which their IDs references are never going to be saved to disk. (This is the default for most entities) |
| Global accessability  | Entity that can be access by any other entity from any other scene. This concept will be used in the Scenes. |
| Local accessability   | Entity that can only be access by run-time entities and other entities in the same scene. This concept will be used in the Scenes. |
| Run-Time              | Runtime Entities are separate in concept from Scene Entities as Scene entities when created from the editor don't have access to the runtime. |
| info Remap Functions   | Add to a component info a function that given a "from entity" and a "to entity" will replace any references |
| Repacking Entity IDs  | This could be done to minimize fragmentation of the entity IDs within one (Local) Scene  |


### Info Remap Function

We could add set of functions in the **xecs::component::type::info** structure that would allow give components the ability to remap an entity ID to another entity ID. The functions could look as like this:

~~~cpp
// References is a vector that the user would need to fill up with Pointers to entity references to be remap 
// Most time would be all entity references 
using collect_references = void( std::vector< xecs::component::type::entity* >& References ) noexcept;
~~~

This could solve the following issues:

1. When a scene loads all entities IDs in the game could be remapped to a new IDs without a problem.
2. Scene-Prefabs would work as well
3. Moving entities from one local-scene to another would be possible without the need of ranges
4. When moving entities from one local-scene to another we could detect any reference by another local entity.

However we would have the following issues:

1. The user would need to know about this detail.
2. Since is user based there could be user errors such the user forgot to include the remapping function.
3. ...

Other directions to explorer

1. Could the properties provide away such the user does not need to worry about adding these functions?
   * We may have a problems with components that does not show these types of properties
   * We would need to formalize the entity property and force people to use that ((**Which is very doable and desirable**))
2. 

### Reference Issues

A reference is when an Entity has a component that contains a reference to an entity. This reference could be dangerous depending the case so the following are a set of rules that we must fallow to make sure everything is ok.

*Moving Single Entity Due to Reference Issues*
| From Accessability | To Accessability | Reference Issues | Reasons |
|:------------------:|:----------------:|:---------------------------:|-----------------|
| Local A            | Local B          | OK | Thanks to the Info Remap Function
| Global A           | Local A          | NOT OK | Since some scenes may be not loaded, some references can't be fixed.<br> It could be fixed if we could track them... but that may be solved later
| Local A            | Global A         | OK     | Thanks to the Info Remap Function |
| Global A           | Global B         | NOT OK | Since some scenes may be not loaded, some references can't be fixed.<br> It could be fixed if we could track them... but that may be solved later |
| Runtime            | Local A          | NOT OK | IMPOSSIBLE, but could thanks to the Info Remap Function |
| Runtime            | Global A         | NOT OK | IMPOSSIBLE, but could thanks to the Info Remap Function |
| Local A            | Runtime          | NOT OK | IMPOSSIBLE, Should not be allowed |
| Global A           | Runtime          | NOT OK | IMPOSSIBLE, Should not be allowed |

Scenes should only contain entities with short term storage in mind. As when the game is saves it should never save any entity that is not a run-time entity. If a scene needs to have an object that needs to be saved in the game the the scene should put a spawner, so that the object becomes a runtime object.

### Moving Issues

Please note that Global Scenes are always loaded but not necessarily edited, while Local Scenes may or may not be loaded at the same time.
Moving a single entity for different types of scenes

| From Scene         | To Scene           | Moving Single Entity Issues       |
|:------------------:|:------------------:|-----------------------------------|
| Local   A          | Local   B          | Entity-ID for hierarchy is reset. (Safe with info Remap Function could be used) |
| Global  A          | Local   A          | Entity-ID for hierarchy is reset. (info Remap Function could be used, but only partially working...) |
| Local   A          | Global  A          | Entity-ID for hierarchy is reset. (Safe with info Remap Function could be used) |
| Global  A          | Global  B          | Entity-ID for hierarchy is reset. (info Remap Function can't be used, or at best partial) |
| Repacking Local A  | Repacking Local A  | Entity-ID for hierarchy is reset. (Safe with info Remap Function could be used) |
| Repacking Global A | Repacking Global A | Entity-ID for hierarchy is reset. (info Remap Function can't be used, or at best partial) |

Merging different types of scenes and their different options

| From Scene         | To Scene         | Merging Scenes Issues             |
|:------------------:|:----------------:|-----------------------------------|
| Local    A         | Local    B       | Entity-ID for hierarchy is reset (Safe with info Remap Function could be used), OR ranges are copied |
| Global   A         | Local    A       | Entity-ID for hierarchy is reset (info Remap Function could be used, but only partially working...), OR ranges are copied |
| Local    A         | Global   A       | Entity-ID for hierarchy is reset (Safe with info Remap Function could be used), OR ranges are copied |
| Global   A         | Global   B       | Entity-ID for hierarchy is reset (info Remap Function could be used, but only partially working...), OR ranges are copied |


### The global Info structure

~~~cpp
    // Cache Line is 64 bytes...
    // Size of global_info = 8*3 = 24bytes ( Would be nice to get it down to 16bytes )
    struct global_info final
    {
        xecs::pool::instance*           m_pPool         {};     // [64bits] Pointer to the Pool in the archetype
        xecs::pool::index               m_PoolIndex     {};     // [32bits] Current Index inside the pool
        validation                      m_Validation    {};     // [32bits] Validation Number
    };
~~~

## Entity IDs

Each Scene needs to have a unique non-overlapping set of index ranges for their entities so that if we choose at some point to merge them we won't override them. However we can not preallocate 16 Million or so worth of global entities for each Scene, just in case we choose to merge them, because the total memory requirements will be too big. To solve this problem we move into a virtual non overlapping space, where we can assign regions of this virtual memory to each Scene so that when a Scene needs to use it, it can allocate from there.

This means that we will need to have a centralize object that needs to keep track of which ranges are assign to which Scenes. This type of object needs to be exclusive modifiable, meaning two users can not be trying to modify at the same time. This is very hard to achieve with a decentralize source control such GIT. To minimize this problem there for the virtual space needs to be broken up in an intelligent way with a low probabilistic error.

The virtual space is defined as 32bits integer which allows for 4,294,967,296 entries. If we break up this space into 500K entity ranges we get ~8k Ranges in total. Which means that if we have the concept of a project object it could keep track which of these 8K ranges are assign to which Scenes. Please note that we don't need to reserve the entire 2^32 space in virtually, if we know the highest range ever used (and we should), the run time just need to reserve up to that block. However in editor mode we may need to reserve the entire thing or plan for some extra space.

* Entities that belong to scenes are only allocated by the editor side. These type of entities can be virtual memory allocated. 
* At runtime only entities of the run-time type needs to be allocated. Entities of this type can be preallocated about 16M or so. Which it will mean that the first 16M/0.5 = 32Blocks are preallocated by the run-ime.

***Example of Project:***
~~~cpp
[ RangeAllocation:4 ]
{ SceneGuid:G  Range:d }
//-----------  --------
 #45354512332   0
 #45354512332   326
 #45354512332   321
 #4535132332    1000
~~~

The Zero IndexRange there for will start after the first 32Blocks from the run time in other words Range 0 = to actual block 33. At runtime there is not allocation of virtual pages, except when a scene loads.

* **Info:** https://docs.microsoft.com/en-us/windows/win32/memory/reserving-and-committing-memory 
  
Here are some numbers to give an idea of different size sub-blocks and how they would map to the total number of sub-blocks, and the number of sub-blocks per range.

| Sizeof( global_info ) == 8 * 3 could pad to 8 * 4 <br> (4096 * 2)/(8 * 4) ==  256 entities per sub-block ( 2 virtual pages worth ) |
|-------------------------------------------------------------------------------|
| 500K entities / 256 = ~2,000 (Total number of sub-blocks for a range)<br> 2^32/0.5 = ~8K total sub-blocks |
| 100K entities / 256 = ~390   (Total number of sub-blocks for a range)<br> 2^32/0.1 = ~42K total sub-blocks |

The actual runtime and editor structure will follow...
~~~cpp
+------------+
| Full Range | Ptr  --> Virtual Unmapped Allocation of X^32 entries (32 initial sub-blocks are always allocated for run-time )
|            | Free --> Pointer to the next free... (Used only for the runtime)
+------------+ 

+---------+      std::vector (Sorted by the fuller Ranges)
| Scene 0 | ---> +---------+
+---------+      |         | Address   --> Which address starts
                 | Range 0 | Hashmap   --> (256/precision) entries hash map that index into the array below key is the value of the u8, used to quickly allocate to the most full sub-page
                 |         | Ptr       --> std::array<u8, ~2000>{ how many entities are allocated per sub-block }
                 |         | nEntities --> Number of allocated entities
                 +---------+
                 |         | 
                 | Range 1 | 
                 |         |
                 +---------+
                 | ...     | 
                 +---------+
~~~

### <u>Entity IDs</u> - Range Map To Scene

We will have a table that will map a particular range to a scene index. This way is easy to know which ranges are already been used as well as identifying which entities map to which scene.

~~~cpp
    auto TableRangeToScene = std::unique_ptr<u16[]>(MaxRanges); // Initialize to 0xffff which will mean that the range has not been allocated to any scene

    PreallocatedRunTime = MaxRuntimeEntities / (# entity per range)
    IndexOfRange        = static_cast<int>(static_cast<std::size_t>(BaseAddressOfGlobalInfo - AddressEntityGlobalInfo) / (# entity per range)) - PreallocatedRunTime
    SceneIndex          = ( IndexOfRange < 0 ) ? (0xffff-1)   // This means that is a Run Time Entity, there for not in a scene
                                               : TableRangeToScene[IndexOfRange];
~~~

We will have also a Scene mgr which keeps track of the scenes

~~~cpp
    auto                   SceneArray = std::array<std::unique_ptr<xecs::scene::instance>, max_scenes_v>
    int                    nScenes    = ...

    Where from the previous code you can SceneArray[SceneIndex]->...
~~~


### <u>Entity IDs</u> - Validation

The validation part of the entity ID would be generated depending on how the entity was created.

* ***Runtime*** - The validation is a sequence that gets incremented every time an entity dies, so the new entity gets it from the last dead entity.
* ***Scene*** - When an entity is created in the editor and there for set in a scene. The Scene will generate a random number for the validation. While this is a bit less safe than just incrementing it eliminates the complexities of keeping tracking of dead global-entities just to keep that sequence going.


---