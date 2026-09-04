<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>

# xECS Documentation

Cross platform **Event Component And Systems**, is a type of *Entity Component and System*.

| WARNING:<br>:warning: | *ALL THESE DOCUMENTATION IS VERY MUCH IN "WORK IN PROGRESS" STATE | 
|:---:|---|

* [Components](xecs_component.md) - Component stuff
* [Archetypes](xecs_archetype.md) - The grouping of entities by components
* [Family](xecs_pool_family.md) - Pool family is the place where the pools are, and the factor out [share-components](xecs_component_types_share.md) 
* [Pool](xecs_pool.md) - Where the entities and its components are store.
* [Structural Changes](xecs_structural_changes.md) - Creation, and Deletion of entities
* [Scene](xecs_scene.md) - Scene topics
* [Prefabs](xecs_prefab.md) - Prefabs topic
* [App Context](xecs_app_context.md) - How xecs is used under different apps

## XECS Structural map

The structural map is an overview of xECS from an structure point of view. There are several structures that could be done to support all the require features for ECS, some of the alternatives can be found [Here](xecs_architectural_alternatives.md). However the selected structural method is while a bit memory wasted is very fast and efficient.

|||
|:---:|------|
| Positives   | * Minimizes the number of archetypes, so makes searches fast<br> * When dealing with all scenes at ones is fast.<br> * No need to search which scene we need to deal with|
| Negatives   | * Memory wasted for an array of max scenes pointers per archetype (expecting MAX_SCENES to be around 16, in a 2D grid there are 3*3=9). This also limits how many scenes you can have open in the editor at the same time. |

~~~c++         
                                                                   Family (start of scene)       
Vector<Archetype>      Archetype                                   +---------------------------+ 
+--------------+       +-------------------------------+   +------>| Shares                    | 
| unique_ptr<> }------>| array<Family*,MAX_SCENES>     |   |       | +---------------------+++ | 
+--------------+       | +---------------------------+ |   |       | | entity   | entity   ||| | 
| unique_ptr<> |   +-----{ ptr                       | |   |       | +---------------------+++ | 
+--------------+   |   | +---------------------------+ |   |       | Pool                      | 
| unique_ptr<> |   |   | | ptr                       }-----+       | +-----------------------+ | 
+--------------+   |   | +---------------------------+ |           | | Component buffers     | | 
+--------------+   |   | | ptr                       | |           | | +-----+-----+-----+++ | | 
+--------------+   |   | +---------------------------+ |           | | | ptr | ptr | ptr ||| | | 
                   |   | +---------------------------+ |           | | +-----+-----+-----+++ | | 
                   |   | +---------------------------+ |           | +-----------------------+ | 
 Vector<Scene>     |   |                               |           +---------------------------+ 
 +------------+    |   |                               |
 | Scene GUID |    |   | Family (start of scene)       |           Family                       
 | IndexSlot  |    |   | +---------------------------+ |           +---------------------------+
 +------------+    +---->| Shares                    }------------>| Shares                    |
 | Scene GUID |        | | +---------------------+++ | |           | +---------------------+++ |
 | IndexSlot  |        | | | entity   | entity   ||| | |           | | entity   | entity   ||| |
 +------------+        | | +---------------------+++ | |           | +---------------------+++ |
 +------------+        | | Pool                      | |           | Pool                      |
 +------------+        | | +-----------------------+ | |           | +-----------------------+ |
 +------------+        | | | Component buffers     }-------+       | | Component buffers     | |
                       | | | +-----+-----+-----+++ | | |   |       | | +-----+-----+-----+++ | |
                       | | | | ptr | ptr | ptr ||| | | |   |       | | | ptr | ptr | ptr ||| | |
                       | | | +--|--+--|--+-----+++ | | |   |       | | +-----+-----+-----+++ | |
                       | | +----|-----|------------+ | |   |       | +-----------------------+ |
                       | +------|-----|--------------+ |   |       +---------------------------+
                       | -------|-----|--------------- |   |
                       +--------|-----|----------------+   |
                                |     |                    |
                         +------+     +------+             |                            
                         |                   |             |                            
                         v                   v             |   Pool                     
                  +-------------+     +-------------+      |   +-----------------------+
                  | Component A |     | Component B |      +-->| Components            |
                  +-------------+     +-------------+          | +-----+-----+-----+++ |
                  | Component A |     | Component B |          | | ptr | ptr | ptr ||| |
                  +-------------+     +-------------+          | +-----+-----+-----+++ |
                  | Component A |     | Component B |          +-----------------------+
                  +-------------+     +-------------+        
                  +-------------+     +-------------+           
                  +-------------+     +-------------+           
                 
~~~           

---