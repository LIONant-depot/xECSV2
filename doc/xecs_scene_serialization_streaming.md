<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Serialization](xecs_scene_serialization.md) / Streaming

Streaming is an important part of the Scene features, because it allows to have levels that extend forever. This should happen Asynchronously from the actual game so that it can appear transparent to the user. To do this the Scene loading needs to gro throw different stages.

* **Creation of Entities -** This can happen Asynchronously and it is the more time consuming part of the loading process.
* **Merging Entities -** This has to happen Synchronously (usually at the end of the frame) so it is very important that happens as fast as possible, and take little time.

Unloading a Scene is equally important as it should be very fast. This step unlike the previous one is fully Synchronous and also happens at the end of the frame.

## Loading - Creation of Entities

Here the challenge is no to step over the xECS as it runs in a different thread. 

* Here common containers such hash tables, the list of Archetype
* Allocation of the xecs::component::type::entity::global_info
* Remapping EntityIDs

Should be thread made thread safe some how. Additional to that as we add entities it should not conflict with the existing runtime entities that the game is running in the background. To solve this problem all the Archetypes created or used by the loading system will have an **exclusive tag** (scene_loading_tag). This means that even if there is an Archetype that matches all the components it won't match the exclusive tag which guarantees that he archetype is only accessible by the loading system.

## Loading - Merging Entities

Ones the Scene has finish creating all the Entities what we will have is a duplication of Archetypes since the ones used for loading have the **scene_loading_tag**. So this second step is about removing the exclusive tag and potentially merging our archetype with existing archetypes. This merging will come down to merging the actual Family inside the Archetypes:

* **Adding Families -** When a family is not located in the old archetype we will simply add it. This is the simple case.
* **Actual Merging -** This could be the more complex step. However we simplify it by just adding the new pool into the family and leave the system to clean up the pools by itself little by little.


## Unloading Scenes

One Scene load it should retain a list of entities ids that were created by the scene itself. This list is also the list used in the remapping of entities. When we unload we need to go throw each entity and destroy them. When we do this is likely that we will delete virtual pages and/or even pools.

When we unload Scene we have the choice to leave behind the global entities that we may have created. So next time when we load the scene we can ask it to skip the loading of the global entities.

---
