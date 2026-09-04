<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Serialization](xecs_scene_serialization.md) / [Entity](xecs_scene_serialization_entity.md) / Raw

Raw entities are entities that don't belong in the prefab instances group which includes:

* **Regular entities** - usually created directly
* **Share-Entities** - created automatically by the system.

## Archetype Types Section

This section indicates which component types the file would will need to read and in which mode they were serialize. Please note that component tags have not data to be saved so the mode for them is ignore.

~~~cpp
// TypeInfo Details: 
//   Guid:0000000000000000   Type:Data   Name:Entity
//   Guid:895F4D644EFB4528   Type:Data   Name:ShareFilter
//   Guid:9912A2744953624C   Type:Data   Name:ReferenceCount
//   Guid:B746E8DB5F35D46F   Type:Share  Name:GridCell
//   Guid:49F9A4F507C9056B   Type:Tag    Name:ExclusiveShareAsData
[ ArchetypeTypes : 5 ]
{ TypeGuid:G         TypeName:s       SerializationMode:c }
//-----------------  ---------------  -------------------
                 #0   "Entity"                1         
  #895F4D644EFB4528   "ShareFilter"           0         
  ...
~~~

|||
|:------------------:|-------------|
| TypeGuid           | is the GUID of the component type info. This is very important that matches with the component. |
| SerializationMode  | When serializing components of a particular type there are 3 kinds of modes that we support.    |

|SerializationModes|Description|
|:--:|-----------|
| 0 | Means that the component type won't save any information. This is typically a component type Tag. Either that or some component had not information to serialize|
| 1 | Means that it will use the serialize function provided by the user. This is a nice fast way to serialize entities. It also supports future proving|
| 2 | Means that the component will be saved by its properties. This is the default mode, and easier mode to use. However it is not as fast as mode 1.|

***Questions:***

> **What happens if the user deletes a component type from the scripts?**<br>
Those components will be removed when loading from the relevant entities.

> **What happens if the components change(add/remove) their properties?**<br>
If the user does not provide any functionality to remap between the old and the new the properties will be ignore. This is true for both modes (property and serialization function). But not matter the system won't crash.

> **Will adding new properties to components work?** <br>
There should work fine. However their value will be whatever their constructor does. 

> **If the user saved the component with the serialize function and then adds properties will it matter?**<br>
No, the serialization system always prioritizes the serialization function over properties. However you do need to keep the serialization function updated.

> **If the user saved the component with properties and then added the serialization function will it matter?**<br>
No, loading of old scenes will be done with the properties system and the future serialization will be done with the serialization function.

> **What about if I serialize with the serialize function and then add properties and removed the serialization function?**<br>
This would be a problem because old scenes still depends on the serialization function. This will cause the component to be created but its values will set the the default constructed values.

## Family Shares Section (Dependent Section)

This section exists only for **Archetype Types Section** that has share-components, if it does not this section wont be included. This section specifies what share-components the entity are under. For each of the share-entity references we also include their type which is a bit verbose since that information exists already in the **Archetype Types Section**.

~~~cpp
[ FamilyShares:d ]
{ Entity:G           ShareTypeGuid:G }
//-----------------  -----------------
  #61F42A111DAF651C  #61F42A111DAF651C
~~~

|||
|:------------------:|-------------|
| Entity             | is the Entity-id which is based on the value which the share-entities saved themselves with in this file. |
| ShareTypeGuid      | Share component guid which should match the Archetype Type Section, so put here for debugging reasons |

<a name="components_section"></a>
## Components Section

The component section does not include any distinct block, rather it starts with the entity component since this is always require, this block also gives the number of entities that we will be dealing with. Note that the reason to serialize component by component is that it improves the performance and possible compression of the file. However when a component serializes with properties this is not as efficient since each component will create a different block.

***Using a serialization function:***
~~~cpp
//Entity
[ 0000000000000000 : 330 ]
{ Entity:G }
//--------
     #0   
     #3   
     ...
~~~


***Using a serialization full function:***

Note that the user can have as many block section here as they want. The system will insert a final section to indicate when a full_function serialization is done.
~~~cpp

[ A : 330 ]
{ Example:d }
//--------
    0   
    3   
    ...

[ B : 330 ]
{ Example:d }
//--------
    0   
    3   
    ...

// This is the completed header, essential to make sure we have reach the end of a full serialize
@[ FullSerialize ]
~~~

***Using the properties:***
~~~cpp
[ Props : 1 ]
{ Name:s            Data:?                        }
//----------------  -----------------------------
  "velocity/Value"  ;v2 0.7647978663 0.6442690492
~~~

|||
|:------------------:|-------------|
| Name               | is the name of the property in string form |
| Data               | is the property type (;v2) and then the data associated with the type 0.74.., 0.62.. Note that v2 was one of the original type of the properties that we indicated in the ***General Sections*** |


Some component types when they have been serialized with their serialize function may include multiple blocks (this will happen when using the full_serialize function). This is desirable as it really helps the file to remain compact but when the component type which serialized these blocks is removed we must be able to skip the blocks. The way this is handle is by making sure that they user utilizes the full_serialize function correctly. The full serialize needs to be able to report a bad header when is reading. Example:

---