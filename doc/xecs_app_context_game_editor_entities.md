<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /> <br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / [Game Editor](xecs_app_context_game_editor.md) / Entities
~~~
........................  ............................
                       .  . 
   Editor (Editor DLL) .  .    MiddleLayer (Game DLL)
                       .  .
  +---------------+    .  .    +----------------+
  |               |    .  .    |                |
  | Editor Entity |----.--.--->| Runtime Entity |
  |               |    .  .    |                |
  +---------------+    .  .    +----------------+
          |            .  .                           
..........|.............  ............................
          |
          |               ............................
          |               . 
          |               .    Game (Game DLL)
          |               .
          |               .    +----------------+
          |               .    |                |
          +---------------.--->| Runtime Entity |
                          .    |                |
                          .    +----------------+
                          .                           
                          ............................
~~~

When adding or editing entities in the editor we will be running the editor instance of xECS. When the game runs the game will load all the entities from the Editor Instance. Since the Editor instance of xECS should be completely in sync with the Game-runtime it is possible for the Run-Time to copy over all the relevant entities from memory if we wanted to. This would allow to very fast start/stop editor states. Some Issues could exists:

* Knowing which scenes are loaded.
* Executing scripts after a scene is loaded

In the editor for every Entity is created we will have a mirror entity. The mirror entity will be the one that deal with organization issues for the editor such been put in a folder. Also having a name, as well as other editor side only components. Note that while those component are  

## Editor Entities

Editor entities are entities that only exist in the editor context. This entities are used to have extra information for game entities in the editor such:

| Component Type | Component Name          | Description |
|----------------|-------------------------|-------------| 
| Share          | editor Scene            | this will be a nice way to group all the entities that belong to one scene |
| Data           | editor Name             | This will be use to help the user add a name to an entity |
| Data           | editor Tag              | Tags used for the editor |
| Data           | editor Reference        | A reference to the game entity |
| Data           | Children                | Will be used for folders |
| Data           | Parent                  | Will be used for folders |
| tag            | editor_selected_tag     | Will be use to determine which object are selected |
| Data           | editor_scene            | Options and such about controlling the scene in the editor |
| Share          | editor Render Icon      | This indicates which icon should render in the editor |
| Data           | editor Prefab Instance  | Contains which properties/component are overriden    |
| Data           | editor Transform        | A transform component with extra options like, dynamic entity vs static. This allows to optimize the Middle-Layer Entity, by adding or removing components depending on what is selected here. |

When editor entities are created they are typically also created under a scene. This scene is parallel to the game entity scene. So if you have one scene in reality you have 2. One for the editor and one for the game. 

The editor will have a hash map that maps between the game-entities as the (key) to the editor-entity as the (value).

One of the abilities that Editor Entities will have it the ability to automatically edit the MiddleLayer entities. So the user could write systems that base on the value of the Transform component it could add or remove components such Rotation, UniformScale, etc... 

## Reloading/Serializing Entities in the Editor

We must deal with Global, Scene, and Archetype Entity-IDs in such away that is fast and they stay synced across all the DLLs. The tricky thing is that the xECS state across the different DLLs may be very different, meaning, Additional Entities may be created in each ECS that does exists in the other DLL. This complicate the common allocation of Entity-ID across all the DLLs.

Global-ID<br>
For global IDs this is not as complex as the fact that the entities are Global means by the very nature that those particular IDs are fully reserve for them. Also global IDs are not really used in the Editor.DLL Entity-ID which removes allot of the complexity.

Scene-ID / Runtime-IDs<br>
These types of ID are allot more complex to deal with, since their IDs are in full competition and like mention before extra entities may exists that may allocate certain IDs creating conflicts with each DLLs.

Scene Entities ID must be remap every time they are loaded anyways. This is an opportunity to link already between the Editor.DLL-Entities and the MiddleLayer.DLL-Entities. For the Game.DLL-Entities they can have the same ID than the MiddleLayer.DLL-Entities because a different set up entities may be allocates. This means that the Editor-DLL must be able to keep track of the entity-ids for all the other DLLs. 

The way to solve this is to provide the Editor-DLL-Entity a component with references.

~~~cpp
struct editor_entity_references
{
    xecs::component::entity m_MiddleLayerEntity{};        // Editor.dll Entity to MiddleLayer.dll Entity
    xecs::component::entity m_GameEntity{};               // Editor.dll Entity to Game.dll        Entity
}
~~~

There are two types of entities that the editor needs to link with:

1. **Scene Entities** - Entities that are created in the middle layer and they describe which entities should be spawn ones the scene gets loaded.
2. **Non-Scene Entities** - Entities that are created from systems and they were originally not part of a Scene. These entities are commonly created in the Game.DLL

For the second case these entities don't exits in the MiddleLayer nor they belong to a Scene. These entities are there for not really editable by the Editor.DLL. However the user may still want to inspect and or change their values even though their values will be lost. To achieve this the editor.dll may create temporary entities with references back to this entities.

The first case could be solved at load-time as all the scene entities when loading they must get new ids, and their references remap to these new ids. Here is where the possibility of setting the Editor-DLL entities references is possible and falls semi gracefully as part of the system. When the Game.DLL runs it can follow the same process as the MiddleLayer.dll. One things to keep in mind is when the Editor is in play mode and the user is editing a component the user may want to bring those values back to the MiddleLayer.Dll entity, in fact this actually is a quite common tweaking pipeline found in many game editors.

The key to solve the first case is in our ability to map the Scene-preload-EntityID with the Editor.dll EntityID. To do that we have a hashmap.
~~~
Key   = Fn( SceneGUID#, MiddleLayer-DLL-EntityID# (before load remapping) )
Value = Editor-DLL-EntityID#
~~~
This hash map is created by the Editor.dll and must be somehow accessible to the MiddleLayer loader so that it can give the Editor.dll Entity the new MiddleLayerEntityID. This will most likely be done with a call back function similar to:
~~~cpp
struct functor
{
    // Returns the EditorEntityID
    virtual xecs::component::entity LinkEditorEntity( xecs::component::entity Pre_remapedEntityID, xecs::component::entity RemapedEntityID ) noexcept=0;
    xecs::scene::guid m_SceneGuid;
    bool              m_isMiddleLayer;
};
~~~

At that point the editor Entity should be able to "see"/know where the MiddleLayer Entity is. For the game.dll this will look identity except that m_isMiddleLayer will be set to false.

Note that for this hold thing to work whenever the Editor.dll loads a Scene it must always be sure to load the MiddleLayerScene as well. Because the IDs in the editor_entity_references must always be kept synchronous, breaking those IDs will result in the editor not knowing how to link the entities.

## Serializing Entities and Prefrab Instances

When serializing MiddleLayer-DLL entities it must know if an entity is a prefab, and if so what are the overrides etc. Since this information is store in the Editor.DLL entities it may not be trivial to retrieve this information. The only real way to address this is when it serializes it must check if each entity with the Editor.DLL entities to see if it is a prefab instance or not. (This problem may be solve if prefab-instances carry along a component that points back to the prefab), however the argument is that not having such a component will help minimize the number of Archetypes.

The system needs to provide a call back to the get the Prefab Information Component so that we can serialize the instance properly. This call will be made from the middle layer.

~~~cpp
// This **may** need to have an pure virtual interface
struct xecs::prefab::editor::component
{
        xecs::component::entity         m_PrefabEntity;
        std::vector<component>          m_lComponents;
};

// This refback could be used by prefab instances or any other entity (if they are scene based)
struct xecs::scene::editor::refback_component
{
        xecs::component::entity         m_EditorID;
};

struct functor 
{
    virtual xecs::prefab::editor::component& getPrefabInstanceDetails( xecs::component::entity EditorEntityID ) noexcept=0;
};
~~~


## Display Entities in the editor

There will be many kinds of views of the entities:

| View Type | Description | 
|-------------------|---|
| Project           | This will be similar to unities solution view. The Scenes will be at the root, after that the editor will display folders which are actually Editor=Entities. Inside the folders we will have Editor-Entities and we can display the name of the entities if they have any or the EntityID if not. There could be two types of folders; Zone folders, and Normal folders. But more on that later. |
| Property          | When selecting an Editor-Entity it will display its properties as well as the Gameplay-Entity properties. |
| Render            | When rendering the Gameplay-Entity will display as expected, Editor-Entity will also display an Icon. Using the position of the Gamplay-Entity if any. |
| Archetype-View    | One possible view is to display Gameplay-Entities base on their Archetype, this case the emphasis is in the Archetype not so much the entities. | 
  


---
