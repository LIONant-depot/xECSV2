<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Serialization](xecs_scene_serialization.md) / [Entity](xecs_scene_serialization_entity.md) / Prefab Instance

When saving prefab instance entities we need to save the minimum information that we need to recreate the instance. When loading we follow these steps:

1. Identify the prefab
2. Change the instance base on possible component changes( such added or moved components )
   * The creation of the entity instance need to have a particular entityID ( entityID given by the file )
3. Change the values of our components to reflect the values of our instance
   * Note that changing the value of a share-component may move the entity to a different family

***Questions:***

> **What happens if we are loading a prefab instance with share components?**<br>
> We handle the following cases:<br>
> 1. The Prefab may have add/removed the share components
> 2. The prefab may have updated the share component values
> 3. The prefab has not changed

> **what happens if the user deletes the prefab?**<br>
The Entity won't be created

> **What happens if the prefab has the same component as the prefab instance says is a new component?**<br>
The component still completely overriden by the prefab instance, and only one copy of the component will be created.

> **What happens if we delete a component that the prefab instance has overrides?**<br>
The component is completely removed from the entity, and the overrides are discarded.

> **What happens if the prefab removes a component that the instance also removed?**<br>
Nothing should work ok.


## PrefabInstance
Unlike Raw Entities Prefabs Instances have to save each entity individually. This means that this header and the following sections will be repeated per instance

This header indicates that we are entering the PrefabInstance context. It contains related information to this context.

~~~cpp
[ PrefabInstance ]
{ EntityID:G  PrefabID:G }
//----------  -----------
      #2          #0     
~~~

|||
|:------------------:|-------------|
| EntityID           | is the Entity ID, this may or may not need to be remap depending if we are in the global section or not |
| PrefabID           | This is the prefabID, Since the prefabs are global-entities their IDs are consider to be good.|

## Components
When we serialize the instance we need to know which components got add, removed, or modified. This block allows us to know this. This block will also be used to update the prefab instance component when applicable.

~~~cpp
[ Components : 1 ]
{ ComponentGuid:G    Type:d }
//-----------------  ------
  #F8E0155B9ABC0972    0   
~~~

|||
|:------------------:|-------------|
| ComponentGuid      | Component type guid, found from xecs::component::type::info_v |
| Type               | Tells what type of overrides we have for this component. |

| Type Value | Description |
|:--:|-------------|
| 0  | New component so all its properties go overriden. It won't care what the prefab says about this component. It will be expecting a Prop block. |
| 1  | Some properties from this component got overriden by the entity. It will be expecting a Prop block. |
| 2  | The component got deleted from the entity. The wont be any component data for this. |

## Relevant information for each of the components

Here is where either are properties serialize related to the the previous header. All components will be serialized using their properties.

~~~cpp
[ Props : 1 ]
{ Name:s            Data:?                        }
//----------------  -----------------------------
  "velocity/Value"  ;v2 0.7647978663 0.6442690492
~~~

|||
|:------------------:|-------------|
| Name               | Name of the property |
| Data               | Data of the property |

---