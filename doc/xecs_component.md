<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Component

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_typedef_serialization.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Components are one of the core parts of an (Event Component System), because they are the objects responsible for the data. The point of an ECS is to organize the data in such way that will minimize cache issue and there for giving the maximum performance of the system. It is important to break up your data is several types of components. Related properties which are often read togethers is a good guide on how to organize your components.

***The following is an example of a component***:
~~~cpp
struct velocity
{
    // Defines the type of the component (Note how it is a compile-time defined variable)
    static constexpr auto typedef_v = xecs::component::type::data{};

    // Example of some properties
    xcore::vector3 m_Value;         // The Value of our velocity (x,y,z) in meters per second
};
~~~

Note that the component has 2 very important parts:

1. **typedef_v -** xECS support several types of component, this specific variable which is inside the structure will specify it.
2. **Properties -** The data of the actual component can be anything, you can have as many properties as you like however understand that xECS could move your component around memory often. xECS supports all proper construction, destruction, moves, and copy operations.

## Organization of Components in Memory

It is very important to organize components in such a way to they don't waste memory as well as accessing them is very fast. One recommendation is to keep the components as compact as possible. So be sure that they are well align so there is not allot of wasted padding in the structure, and use the smaller types possible to represent your data. xECS will organize the components in pools. These pools are basically work like a vector of each of the components. This means that when a System needs to access them they will all be in a nice sequential order which is very good for the cache.

The organization of xECS

~~~cpp
 Vector<Archetype>      Archetype
+--------------+        +-------------------------------+
| unique_ptr<> |------->| Family                        |                      Family
+--------------+        | +---------------------------+ |                      +---------------------------+
| unique_ptr<> |        | | Shares                    |----------------------->| Shares                    |--->  null
+--------------+        | | +---------------------+++ | |                      | +---------------------+++ |
| unique_ptr<> |        | | | entity   | entity   ||| | |                      | | entity   | entity   ||| |
+--------------+        | | +---------------------+++ | |                      | +---------------------+++ |
+--------------+        | | Pool                      | |                      | Pool                      |
+--------------+        | | +-----------------------+ | |                      | +-----------------------+ |
                        | | | Component buffers     |-------+                  | | Component buffers     |----> null
                        | | | +-----+-----+-----+++ | | |   |                  | | +-----+-----+-----+++ | |
                        | | | | ptr | ptr | ptr ||| | | |   |                  | | | ptr | ptr | ptr ||| | |
                        | | | +-----+-----+-----+++ | | |   |                  | | +-----+-----+-----+++ | |
                        | | +----|-----|------------+ | |   |                  | +-----------------------+ |
                        | +------|-----|--------------+ |   |                  +---------------------------+
                        +--------|-----|----------------+   |  
                                 |     |                    |      Pool
                          +------+     +------+             |      +-----------------------+        
                          |                   |             +----->| Components            |--> null
                          v                   v                    | +-----+-----+-----+++ |        
                   +-------------+     +-------------+             | | ptr | ptr | ptr ||| |        
                   | Component A |     | Component B |             | +-----+-----+-----+++ |        
                   +-------------+     +-------------+             +-----------------------+        
                   | Component A |     | Component B |             
                   +-------------+     +-------------+             
                   | Component A |     | Component B |             
                   +-------------+     +-------------+             
                   +-------------+     +-------------+             
                   +-------------+     +-------------+             
~~~             
                
---