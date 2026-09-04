<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /> <br>
# [xECS](xecs.md) / [Components](xecs_component.md) / [Types](component_types.md) / Share

Share components are components that are been share with other entities. Why is this desirable?

* **Less memory usage -** Since a component is been shared less instances are needed to do whatever we need to. All memory must go first through the cache by using less memory we use less cache.
* **Better grouping -** , Allow us to group entities base on things that they have in common, which allow for even better cache usage; This grouping we call it "Families".
* **More efficient Queries -** Share components can have a filter back to all the entities that reference them. This allows for fast query based by value rather than just types which for share components is very performant. 

## Key Value

A peculiarity of a share component is the requirement that they must have a unique key value. This key value is how xECS uniquely identifies a share component. The default behavior is to full a full CRC32 of the actual component raw data. However as a user you can provide a better/faster way to do so.

## Build Filter

Building a filter in a share component means that we are going to create a reference back to all the entities that refers to this particular share component. This allow us to create systems that allow us to quickly go throw all the entities that are referring to this share component. A typical use case is if you want to render all the "cats" Entities together. However it is discourage to be using the filter as a default setting because it cost memory and performance.

## Family

[Families](xecs_pool_family.md) represents a unique group of share-components values. As more [entities](xecs_component_entity.md) are inserted in the [Archetype](xecs_archetype.md) and those [Entities](xecs_component_entity.md), they first will be a search for the right [family](xecs_pool_family.md) for that [entity](xecs_component_entity.md) if it can not find it then it will create a new [family](xecs_pool_family.md) to contain that [entity](xecs_component_entity.md).


### Questions

**Can the user change a share component directly?**
In xECS that atomic unity is the Entity, so there is not way to change a share component directly, which means you need to go throw a system like any other component.

**What happens if I have an entity which has a share component with a box and I change it to a cat?**
That entity will be moved from the family that has the share containing the box to the family with the share of a cat.

**What happens if after a change my entity from box to cat it was the last entity in the family of box?**
What xECS should do is to move the entity to their new family and delete the previous family, since there are not more entities in there. By deleting those families the references to the share will be decrease and so shares which their references reach zero will be deleted. However we may add a flag to share component types so that it leaves a global share alone even when it reaches zero references.

**What happens if before loading a scene I delete a particular share type?**
The Loader will do its base to recreate the scene assuming that this share never existed. 

**Can we order the families in a particular way?**
:warning: xECS does not provide any facilities to organize the families in a particular way, this could be added in the future. Organizing the families in particular ways could help in terms of cache since it will invalidate even less the memory.

## <span style="color:red"> :warning: Scope / bGlobalScoped</span>

I will be removing this concept from xECS as it seems not to be a good match.

<s>
The scope is how we control/narrow the intend usage of our share component types. There are 3 types of scopes:

* **Global -** Share components that are going to be share across all entities independently of archetype or families. This means that globally there is only one instance of a share component with a particular value.
* **Archetype -** This means that an instance of a share component with the same value will be repeated per archetype. Why do we want this? Because it allows for bulk modification of entities via changing the share component directly. Doing so will can create a cascading effect in the system.
* **Family -** This means that an instance of a share component with the same value will be repeated per archetype. Why do we want this? Because it allows for bulk modification of entities via changing the share component directly. Doing so will can create a cascading effect in the system.
</s>


---
