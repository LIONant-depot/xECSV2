<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Serialization](xecs_scene_serialization.md) / Ranges

This section is part of saving global entities. This entities are entities which their ids are allocated inside pre-allocated ranges. This ranges allows to remove conflicts with any other scene. When a particular scene runs out of ranges it must request them via the editor or by exclusively checking out the [project_global.ranges file](xecs_scene_ranges_serialization.md).

## Ranges Section

There is only one block in this section and it just give you the address of each range. Note that it has not information on how many entities there is in each range. This is indirectly acquired by the global-entity entity-id. This makes the file lighter in terms of data and remove potential discrepancy issues. Note that some ranges wont have any entities in them as it is typical that scene would over allocate ranges to minimize how many times to allocate them as it can be an expensive and inconvenient process.

~~~cpp
[ Ranges : 3 ]
{ Address:G }
//--------------
  #234FA43234431
  #234FA43234432
  ...
~~~

|||
|:------------------:|-------------|
| Address            | The actual range address. Each range has a fixed size of 16,384 entities. |

---
