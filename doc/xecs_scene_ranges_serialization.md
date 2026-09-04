<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / [Ranges](xecs_scene_ranges.md) / Serialization

This file ***scene_global.ranges*** is where xECS allocates/assign globally ranges to Scenes. This file should always be exclusively checkout by a user so that no other user can edit the file at the same time. This is important because if this is not possible you can have ranges which overlap which will corrupt the project.

## FileInfo Section

In order to know which version of the serialization we are using we will save a Label, Version, Date and Time.

~~~cpp
[ FileInfo ]
{ Type:s                  Version:ddd  AppCompilationDate:s   CreationDate:s     User:s }
//----------------------  -----------  --------------------  ------------------  --------------
  "xECS - Scene Ranges"     1  0  3     "21:32:4 2/4/2022"   "21:32:4 2/4/2022"  "Pepe"
~~~

|||
|:------------------:|-------------|
| Type               | Type of file that this is | 
| Version            | Major: Breaking changes of the file format <br> Minor: changes does not break the API but it has a significant change none the less <br> Patches: Bug fixes | 
| AppCompilationDate | The time when the application was compiled |
| CreationDate       | The time when the file was created/exported |
| User               | Email or Name of the user |

## Metrics Section

This block servers to make sure that the application and the file are 100% in sync. If the number below is different from the application the entire project is in danger of corruption.

~~~cpp
[ Metrics ]
{ RangeSize:d    
//-------------- 
  16384          
~~~

|||
|:------------------:|-------------|
| RangeSize          | Is how big a range is interns of entities. In this case there are 16384 entities in a range. |

## Allocation Section

This section should have all the ranges allocated for the project. The largest number in this list will help indicate how much virtual memory will be allocated at runtime. The hope is that all ranges will be completely full of entities, however this may not be necessary the case. But xECS maps the virtual pages into physical pages only when needed so it wont waste memory. 

~~~cpp
[ Allocation : 3 ]
{ Address:G       Scene:G   }
//--------------  -----------
  #234FA43234431  #24F43431
  #234FA43234432  #24F43431
  ...
~~~

|||
|:------------------:|-------------|
| Address            | Which Range Address was allocated for a particular Scene. |
| Scene              | The Scene GUID, which uniquely identifies a Scene |


---