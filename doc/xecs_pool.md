<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Pool

Pools are where all the [components](xecs_component.md) are allocated from. Pools are linear memory so that iteration is simple and fast. To keep the iteration linear it is very important for the pool to move entities from the back of the list to fill up the hole. A [Family](xecs_pool_family.md) can add more pools when they run out of space. These pools are added via a link-list. When there are several pool in a [Family](xecs_pool_family.md) the last pool is consider to be the end of the list. Which means that if there are 3 pools and you delete an entity from the first one, another entity from pool 3 will come and fill the hole. So only the last pool is the one that has space to add more entities.

## Memory

The pool has N allocations of virtual memory. N is the number of components an entity has, this includes the [Entity](xecs_component_entity.md) component as well. The amount of memory allocated in each allocation depends on the size of the component size rounded up to the nearest virtual page size (4k). Each allocation should be able to host **xecs::settings::max_entity_count_per_pool_v** which right now is setup to be 50,000. Ones a pool is full a new one will be created as explained. 

The virtual memory is reserved but not committed. Which means that we are reserving the space in the virtual address but actually not linking it with physical memory. We commit is virtual page as needed, which means that pools should be fairly efficient from a memory usage point of view.

When the pool can release a virtual page because there are not component using it, then the pool will release the page. This will make sure that only the memory that is actually needed will be allocated.

## Component functions for pools

Since the pools are in charge of memory they need to know what to do when they deal with components.

| Operation | Description |
|:---:|---|
| Destructor   | When destroying a component the pool will check if the component has a destructor if it has then it will call ir other wise nothing will happen |
| Construction | When constructing a component the pool will check if the component has a void constructor if it does it will call it if it does not then it won't touch any of the memory |
| Moving       | When components are moved around it will check if the component has a move constructor if it has then it will try to used it to move the component into the right place, other wise a memcpy will be used, and a destructor (if it has any) will be called ) |
| Copying      | Some time components are duplicated such when using [prefabs](xecs_prefab.md) when this happens the pool will check for the copy constructor, otherwise a memcpy will be used. |

This means that component should be able to support virtual functions. 

---
