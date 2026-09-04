<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / Game Editor

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

There are many possible types of [Editor Architectures](xecs_app_context_game_editor_architectures.md) that xECS can support and it is up to the user to select the best one for it. However we will focus on a simple Layer One architecture to prioritize performance. 

~~~
                              +------------------------+                         | Components
                              |                        |                         |      + Mirrors
              +-------------->| xECS Editor-DLL        |------------------------>|      + Composition
              |               |                        |                         |      + Editor Entity Spawners
              |               +-----------+------------+                         |      + Editor Entity Prefabs
              |                           |    
    +---------+---------+                 |        +------------------------+      
    |                   |                 |        |                        |      | Game Scripting Directory
    | App: Game Editor  |                 +------->| xECS Editor Game-DLL   |----->|      + Component Directory
    |                   |                          |                        |      |      + System Directory
    +---------+---------+                          +------------------------+      
              |                                   
              |               +------------------------+
              |               |                        |
              +-------------->| xECS Runtime Game-DLL  |
                              |                        |
                              +------------------------+
~~~

While the diagram shows 3 different DLLs in reality we just have two:

***Game.DLL***
Also Known as the "xECS Editor Game-DLL" and "xECS Runtime Game-DLL". In reality is the exact same DLL but it is duplicated so that we can instantiated more than ones. Because the first case will be used together with the editor to create entities etc, the second will be used just to run the game. One way to think of the Game-DLL is like a scripting DLL. Here is where game programmers will be spending most of their time.

When new components or systems are added the new Game.DLL will get recompiled. After that it will create a copy of the dll for the editor. The Editor actually needs to save itself and reload the data again in order to make sure the data matches the changes in components if any. Note that the Editor-DLL does not need to be reloaded. 

***Editor.DLL***
Also Known as the "xECS Editor-DLL", this DLL is where all the system and editor components are. The [Editor Entities](xecs_editor_entities.md) are different because they specialize in the specific needs of the editor. This allows us to have add additional components which are useful in the editor context with out polluting the actual game run-time entity.

~~~cpp
struct link
{
    xecs::component::entity   m_Entity;
};
~~~



---