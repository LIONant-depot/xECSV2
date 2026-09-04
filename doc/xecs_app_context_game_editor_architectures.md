<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [App Context](xecs_app_context.md) / [Game Editor](xecs_app_context_game_editor.md) / Architectures

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

xECS does not force a particular editor Architectures for the user so the possibilities are open. The best way to think about possible Architectures is how many layers of abstraction do you want for the user. However not matter of the Architecture it is recommended that the editor runs with 3 logical types of DLLs.

Component and System can have different usages and target audiences, so it is a good idea to try to create clear domains for them:

| Concept | Description  |
|---------|---|
| Core System/Components     | Elements such rendering, physics, etc. Thing that designers will never change directly. |
| Gameplay System/Components | Elements that are in the designers's domain |
| Editor System/Components   | Elements used by the editor, which are not part of the runtime |

## Layer One Editor Architectures

What distinguish the type of architecture is the simplicity, here the MiddleLayer_DLL is identical to the Game_DLL. This creates for a simple and direct system for any member of team to work in.

~~~
                                          LAYER ONE           
                                 =============================
                                  +------------------------+ 
                                  |                        | 
      +---------------+    +----->| Editor-DLL             |  Editor Components and Systems, Provides the Core Editor Context
      |               |    |      |                        | 
      | Application   |----+      +------------------------+ 
      |               |    |                        
      +---------------+    |      +------------------------+ 
                           |      |                        | 
                           +----->| MiddleLayer-DLL        |  Gameplay and Core Components and Systems, Provides Scene Editor Context
                           |      |                        | 
                           |      +------------------------+
                           |
                           |      +------------------------+ 
                           |      |                        |  
                           +----->| Game-DLL               |  Gameplay and Core Components and Systems, Provides the Runtime Context
                                  |                        | 
                                  +------------------------+
~~~

| DLL TYpes  | Description |
|:------------:|-------|
| Editor_DLL   | This runs the entities for the editor. This is pretty much independent for any game. Note that this DLL could be merge with the actual Application. |
| MiddleLayer_DLL | This DLL is identical to the Game-DLL but it is used differently. For example it needs to know some key details about the game, like which systems it has, what kind of properties the components have but it won't run any of its systems. |
| Game_DLL | Runs the actual game when testing it in the editor, so it must include all the core systems that the game needs to run. This DLL does not even need to be loaded most of the time, it only really requires to be loaded when the user hits plays in the editor. |

From the context of the editor what happens when you edit components, systems or even xECS?

| User Update                            | Occurrence Rate | DLL Reload |
|:--------------------------------------:|:---------------:|------------|
| Game-System                            | Hight           | * None (Game-DLL When Needed)|
| Game-Components                        | Hight           | * MiddleLayer-DLL |
| Core-System                            | Medium          | * None or MiddleLayer-DLL |
| Core-Components                        | Hight           | * MiddleLayer-DLL |
| Editor System                          | Low             | * Editor-DLL |
| Editor-Component                       | Low             | * Editor-DLL |
| Hit Play                               | Very Hight      | * Game-DLL |

## Layer Two Editor Architectures

The main reason to have this type of architecture is to add the concept of scripting. Which means that we can separate core functionality from design functionality. This allows to support scripting languages at the cost of performance. However things like C# can be integrated with debugging capabilities. 

By separating the concepts as shown in the image below we can talk about the possibility of merging different elements, example; Merging Application w/ Editor-DLL and MiddleLayer-DLL or Merging Game-DLL w/ Scripting-DLL. By merging these elements you can see how Layer two could decay to Layer one, or other variants.

~~~
                                    LAYER ONE (Core Layer)        LAYER TWO (Scripting Layer)
                                 =============================   ==============================
                                  +------------------------+ 
                                  |                        | 
      +---------------+    +----->| Editor-DLL             |  Editor Components and Systems, Provides the Core Editor Context
      |               |    |      |                        | 
      | Application   |----+      +------------------------+ 
      |               |    |                        
      +---------------+    |      +------------------------+ 
                           |      |                        | 
                           +----->| MiddleLayer-DLL        |  Core Component and Systems, Provides the Editor Context
                           |      |                        | 
                           |      +------------------------+
                           |                    |                +------------------------+
                           |                    |                |                        |
                           |                    +--------------->| Scripting_DLL          | Game Components and Systems
                           |                                     |                        |
                           |                                     +------------------------+
                           |      +------------------------+ 
                           |      |                        | 
                           +----->| Game-DLL               |  Core Component and Systems, Provides Runtime Context
                                  |                        | 
                                  +------------------------+
                                                |                +------------------------+
                                                |                |                        |
                                                +--------------->| Scripting_DLL          | Game Components and Systems
                                                                 |                        |
                                                                 +------------------------+
~~~

| DLL TYpes  | Description |
|:------------:|-------|
| MiddleLayer_DLL | This DLL deals with the core components and system of the game such render, physics, etc. It also needs to be able to load the Scripting_DLL since the editor will need to know about the system and components. |
| Editor_DLL   | This runs the entities for the editor, which is the main data base from the context of the editor. This is pretty much independent for any part of the game. Note that in this type of architectures the Editor-DLL and the MiddleLayer-DLL could be merged together, additionally they could also be merge is with the Application itself.  |
| Game_DLL | Runs the actual game when testing it in the editor, so it must include all the core systems that the game needs to run. This DLL does not even need to be loaded most of the time, it only really requires to be loaded when the user hits plays in the editor. |
| Scripting_DLL | This DLLs main job is to provide designer focus components and system to the game. Note that if the scripting is C++, the Game-DLL could link the scripting code directly into it (which is what they layer one Architecture does). |

When the Reloading of DLLs happens?

| User Updates                             | Occurrence Rate | DLL Reload |
|:----------------------------------------:|:---------------:|---------|
| Game-Systems                             | Hight           | * None (Game-DLL When Needed) |
| Game-Components                          | Hight           | * MiddleLayer's Scripting-DLL|
| Core-Systems                             | Middle          | * None or MiddleLayer-DLL w/ Scripting-DLL |
| Core-Components                          | Middle          | * MiddleLayer-DLL w/ Scripting-DLL (Game-DLL When Needed) |
| Editor-System                            | Low             | * Editor-DLL <br> |
| Editor-Components                        | Low             | * Editor-DLL <br> |
| Hit Play                                 | Very Hight      | * Game-DLL w/ Scripting-DLL |

---



