<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Pool](xecs_pool.md) / Family

[Entities](xecs_component_entity.md) are grouped into [Archetypes](xecs_archetype.md), however inside an [Archetype](xecs_archetype.md) [Entities](xecs_component_entity.md) are group into families. What is a family? There is always at least one family in each [Archetype](xecs_archetype.md).Families have two distinct responsibilities:

1. Families are responsible of component [Pools](xecs_pool.md). The family could have several [Pools](xecs_pool.md) which is where the entities are. These pools are kept in a link-list and entities should be contiguous inside each pool, even across the link-list. So that the very last pool should be the only place where there is space for more entities. 
2. Families represents a unique group of [share-components](xecs_component_types_share.md) values. Whenever there is a new set of unique values for [share-components](xecs_component_types_share.md) in the [Archetype](xecs_archetype.md) a new family will be created. So the family represents the factor out [share-components](xecs_component_types_share.md) for any [entities](xecs_component_entity.md) inside it. Lets have a simple example first to illustrate this concent:

Let's say I have two entities instances in one archetype. Lets also say that this Archetype has a share component. Lets call this [share-component](xecs_component_types_share.md) the *render component*. Since we have two entities lets say one entity has a *Render Component* with the value "Box" and the other Entity has a *Render Component* with its value set to "Cat". This will mean that we have to different families. One family with a share component of value "Box", and another family which has another share component of a value "Cat". Each entity will be inside their respective family. 

*Example Visualization*
~~~
+-----------+
| Archetype | ---> +---------+
+-----------+      |         |      +----------+
                   | Family  | ---> | Entity 1 |
                   | "Box"   |      +----------+
                   +---------+      
                   |         |      +----------+
                   | Family  | ---> | Entity 2 |
                   | "Cat"   |      +----------+
                   +---------+ 
~~~

So the generic form then is...

~~~
+-----------+
| Archetype | ---> +---------+
+-----------+      |         |      +--------+--------+--------+
                   | Family  | ---> | Entity | Entity | ....   |
                   |         |      +--------+--------+--------+
                   +---------+      
                   |         |      +--------+--------+--------+
                   | Family  | ---> | Entity | Entity | ....   |
                   |         |      +--------+--------+--------+
                   +---------+ 
                   |         |
                   | ...     |
                   |         |
                   +---------+
~~~

So what happens if stead of a single share component you have two share component ( A *Render Component*, and a *Collider Component*) per entity? But this time you have 3 entities.
1. **Entity** with (Share Components) *Render Component* = "cat", *Collider Component* = "sphere"
2. **Entity** with (Share Components) *Render Component* = "box", *Collider Component* = "sphere"
3. **Entity** with (Share Components) *Render Component* = "box", *Collider Component* = "box"

~~~
+-----------+
| Archetype | ---> +---------+
+-----------+      |         |      +----------+
                   | Family  | ---> | Entity 1 |
                   | "cat"   |      +----------+
                   | "sphere"|
                   +---------+      
                   |         |      +----------+
                   | Family  | ---> | Entity 2 |
                   | "box"   |      +----------+
                   | "sphere"|
                   +---------+ 
                   |         |      +----------+
                   | Family  | ---> | Entity 3 |
                   | "box"   |      +----------+
                   | "box"   |
                   +---------+
~~~

## Structural Overview
~~~cpp
                                                  Family (start of scene)       
      Archetype                                   +---------------------------+ 
      +-------------------------------+   +------>| Shares                    | 
      | array<Family*,MAX_SCENES>     |   |       | +---------------------+++ | 
      | +---------------------------+ |   |       | | entity   | entity   ||| | 
  +-----{ ptr                       | |   |       | +---------------------+++ | 
  |   | +---------------------------+ |   |       | Pool                      | 
  |   | | ptr                       }-----+       | +-----------------------+ | 
  |   | +---------------------------+ |           | | Component buffers     | | 
  |   | | ptr                       | |           | | +-----+-----+-----+++ | | 
  |   | +---------------------------+ |           | | | ptr | ptr | ptr ||| | | 
  |   | +---------------------------+ |           | | +-----+-----+-----+++ | | 
  |   | +---------------------------+ |           | +-----------------------+ | 
  |   |                               |           +---------------------------+ 
  |   |                               |
  |   | Family (start of scene)       |           Family                       
  |   | +---------------------------+ |           +---------------------------+
  +---->| Shares                    }------------>| Shares                    |
      | | +---------------------+++ | |           | +---------------------+++ |
      | | | entity   | entity   ||| | |           | | entity   | entity   ||| |
      | | +---------------------+++ | |           | +---------------------+++ |
      | | Pool                      | |           | Pool                      |
      | | +-----------------------+ | |           | +-----------------------+ |
      | | | Component buffers     }-------+       | | Component buffers     | |
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

## Memory

Note how there is one Family always inweaved in the [Archetype](xecs_archetype.md). This is because most offen an Archetype will be used with no [share-component](xecs_component_types_share.md) and one Scene, any lesser than this and arguably the [Archetype](xecs_archetype.md) should be deleted. If you look closely the Family also has a [Pool](xecs_pool.md) always inweaved in it as well, for similar reasons. Which means most time the single allocation of the [Archetype](xecs_archetype.md) is enough. 

## Scene

As you can see an [Archetype](xecs_archetype.md) has pointers to Family. Each slot in that array represents a different [Scene](xecs_scene.md), which means that every family belongs to a particular [Scene](xecs_scene.md). This allows xECS to load [Scene](xecs_scene.md) in the background, or to turn [Scene](xecs_scene.md) On or Off.

## GUID

The formula to compute the Family's GUID is:
~~~cpp
Family_GUID = Archetype::GUID.m_Value + SUM_ALL( Share-Component::Key.m_Value ) + Scene::GUID.m_Value
~~~

Every family has a Global Unique Identifier (GUID), which is used to quickly find a particular family via a **std::unordered_map**. This map can be found in **xecs::archetype::mgr::m_PoolFamily**. However the owner of the Family is the [Archetype](xecs_archetype.md).


---