<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Architectural Alternatives

When deciding how to organize xECS from the point of view of structures while supporting all the require features, yet maximizing performance and minimizing allocations the following methods were consider.


### Method A (Alternative)

|||
|:---:|------|
| Positives   | * Minimizes the number of archetypes, so makes searches fast<br> * When dealing with all scenes at ones is fast. |
| Negatives   | * When dealing with a particular set of scenes a search for which sub-scene must be made for every archetype<br> * New structures (SubScene)<br>|

~~~c++
Vector<Archetype>  Archetype                                                                   SubScene                          
+--------------+   +-----------------------------------+                                       +-------------------------------+ 
| unique_ptr<> }-->| SubScene                          |   +---------------------------------->| Family                        |
+--------------+   | +-------------------------------+ |   |                                   | +---------------------------+ | 
| unique_ptr<> |   | | Family                        }-----+   Family                          | | Shares                    | |
+--------------+   | | +---------------------------+ | |       +---------------------------+   | | +---------------------+++ | | 
| unique_ptr<> |   | | | Shares                    }---------->| Shares                    |   | | | entity   | entity   ||| | | 
+--------------+   | | | +---------------------+++ | | |       | +---------------------+++ |   | | +---------------------+++ | | 
+--------------+   | | | | entity   | entity   ||| | | |       | | entity   | entity   ||| |   | | Pool                      | | 
+--------------+   | | | +---------------------+++ | | |       | +---------------------+++ |   | | +-----------------------+ | | 
                   | | | Pool                      | | |       | Pool                      |   | | | Component buffers     | | |
                   | | | +-----------------------+ | | |       | +-----------------------+ |   | | | +-----+-----+-----+++ | | | 
                   | | | | Component buffers     }---------+   | | Component buffers     | |   | | | | ptr | ptr | ptr ||| | | | 
                   | | | | +-----+-----+-----+++ | | | |   |   | | +-----+-----+-----+++ | |   | | | +-----+-----+-----+++ | | | 
                   | | | | | ptr | ptr | ptr ||| | | | |   |   | | | ptr | ptr | ptr ||| | |   | | +-----------------------+ | | 
                   | | | | +--|--+--|--+-----+++ | | | |   |   | | +-----+-----+-----+++ | |   | +---------------------------+ | 
                   | | | +----|-----|------------+ | | |   |   | +-----------------------+ |   +-------------------------------+ 
                   | | +------|-----|--------------+ | |   |   +---------------------------+             
                   | +--------|-----|----------------+ |   |   
                   +----------|-----|------------------+   |   
                              |     |                      |                            
                       +------+     +------+               |                            
                       |                   |               |                            
                       v                   v               |   Pool                     
                +-------------+     +-------------+        |   +-----------------------+
                | Component A |     | Component B |        +-->| Components            |
                +-------------+     +-------------+            | +-----+-----+-----+++ |
                | Component A |     | Component B |            | | ptr | ptr | ptr ||| |
                +-------------+     +-------------+            | +-----+-----+-----+++ |
                | Component A |     | Component B |            +-----------------------+
                +-------------+     +-------------+          
                +-------------+     +-------------+             
                +-------------+     +-------------+             
~~~           


### Method B (Alternative)

|||
|:---:|------|
| Positives   | * No need to search which scene we need to deal with<br> * More compact representation for Scenes  |
| Negatives   | * The number of archetypes is multiplied by the number of Scenes <br> * Search queries is are slower because they are multiplied by the number of scenes. <br> * Search queries are larger in memory|

~~~cpp
Vector<Scene>     Vector<Archetype>   Archetype
+------------+    +--------------+    +-------------------------------+
| Scene      }--->| unique_ptr<> }--->| Family                        |       Family
+------------+    +--------------+    | +---------------------------+ |       +---------------------------+
|            |    | unique_ptr<> |    | | Shares                    }-------->| Shares                    |
+------------+    +--------------+    | | +---------------------+++ | |       | +---------------------+++ |
+------------+    | unique_ptr<> |    | | | entity   | entity   ||| | |       | | entity   | entity   ||| |
+------------+    +--------------+    | | +---------------------+++ | |       | +---------------------+++ |
                  +--------------+    | | Pool                      | |       | Pool                      |
                  +--------------+    | | +-----------------------+ | |       | +-----------------------+ |
                                      | | | Component buffers     }-------+   | | Component buffers     | |
                                      | | | +-----+-----+-----+++ | | |   |   | | +-----+-----+-----+++ | |
                                      | | | | ptr | ptr | ptr ||| | | |   |   | | | ptr | ptr | ptr ||| | |
                                      | | | +--|--+--|--+-----+++ | | |   |   | | +-----+-----+-----+++ | |
                                      | | +----|-----|------------+ | |   |   | +-----------------------+ |
                                      | +------|-----|--------------+ |   |   +---------------------------+
                                      +--------|-----|----------------+   |   
                                               |     |                    |       Pool
                                        +------+     +------+             |       +-----------------------+
                                        |                   |             +------>| Components            |
                                        v                   v                     | +-----+-----+-----+++ |
                                 +-------------+     +-------------+              | | ptr | ptr | ptr ||| |
                                 | Component A |     | Component B |              | +-----+-----+-----+++ |
                                 +-------------+     +-------------+              +-----------------------+
                                 | Component A |     | Component B |
                                 +-------------+     +-------------+
                                 | Component A |     | Component B |
                                 +-------------+     +-------------+
                                 +-------------+     +-------------+
                                 +-------------+     +-------------+
~~~


---