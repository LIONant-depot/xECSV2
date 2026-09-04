<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / Ranges

A Scene range is a particular allocation of **xecs::component::type::entity::global_info**. Since all the global_infos are in a continues virtual array the address of each one of them is unique. A range is therefor a discrete chunk of a fixed size from this array. **A range is 16,384 (entities == global_infos)**, so whenever a scene runs out of entities it must allocate another range. To minimize how many times a Scene needs to reserver ranges (since that is an expensive operation) it may reserver several ranges at ones.

~~~cpp
|----------------------------------------------------------------------------------------------------|
| Fact about Ranges                                                                                  |
|----------------------------------------------------------------------------------------------------|
| Addressable Entity Index  = 2^32                                                                   |
| Info Size                 = 16 bytes <= sizeof( xecs::component::type::entity::global_info )       |
| entity Size               = 1 info size <= ( Since there is one global_info per entity )           |
| Cache Line                = 64 bytes                                                               |
| Virtual Page Size         = 4096 bytes                                                             |
| Sub-Range Size (bytes)    = Virtual Page Size                                                      |
| Sub-Range Size (entities) = 256 entities <= Virtual Page Size / Info Size                          |
| Range Size (sub-ranges)   = 64 sub-ranges <= (( Chosen by the architect ))                         |
| Range Size (entities)     = 16,384 entities <= (Range(sub ranges)) * (Sub-Range Size(entities))    |
| Range Size (byte)         = 262,144 KB <= (Range Size (sub-ranges)) * (Sub-Range Size (bytes))     |
| Total Ranges              = 262,144 ranges <= (Addressable Entity Index) / (Range Size(entities))  |
| Max Entities              = 4,294,967,296 entities <= Total Ranges * (Range Size(entities))        |
| Max Memory Usage          = 68.719,476,736 GB <= Max Entities * Info Size                          |
|----------------------------------------------------------------------------------------------------|
~~~

## Runtime preallocation of ranges

There are a few ranges that are preallocated by the system for the runtime. This set represents the maximum size that the runtime can be. The runtime will allocate the ranges as needed, which means that it won't wasteful in terms of memory.

~~~cpp
|----------------------------------------------------------------------------------------------------|
| Preallocated Ranges = 8,192 (( Chosen by the architect ))                                          |
| Total Sub-Ranges    = 4,194,304 sub-ranges <= Preallocated Ranges * (Range Size (sub-ranges))      |
| Total Entities      = 134,217,728 entities <= Preallocated Ranges * Range Size (entities)          |
| Total Memory Usage  = 2.147,483,648 GB (virtually reserved) <= Total Entities * Info Size          |
|----------------------------------------------------------------------------------------------------|
~~~

## Range structure and runtime requirements

Since the entire range needs to be continuous the runtime must allocate base on its needs and the needs of the scenes. In order to be smart no wasting too much virtual address space and memory for the editor case the system will first allocate the current maximum range required by the scenes plus 1,024 Additional ranges as buffer. If it runs out of ranges it would need to realloc the entire set.

The actual runtime and editor structure will follow...
~~~cpp
+------------+
| Full Range | Ptr           --> Virtual Unmapped Pre-Allocated of what is likely needed plus the buffer
|            | Free          --> Pointer to the next free... (Used only for the runtime)
+------------+ LastSub-Range --> Number which indicates which sub-range was last allocated so it knows when it runs out of memory which other sub-range to alloc
~~~

Scenes will look as follows...
~~~cpp
+---------+      std::vector (Sorted by Address Range.Number )
| Scene 0 | ---> +---------+
+---------+      |         | Number           --> Range Number where it starts
                 | Range 0 | SubRanges        --> std::array<u8, 64>{ Tells how many entries are in used in this range,
                 |         |                                          when full it will turn on the bit in the FreeSubrangeMask }
                 |         | FreeSubrangeMask --> u64 of free subranges
                 |         | VirtualMapMask   --> u64 of mapped subranges
                 +---------+
                 |         | 
                 | Range 1 | 
                 |         |
                 +---------+
                 | ...     | 
                 +---------+
~~~

So when a Global Entity is allocated, the way we alloc its global_info from a Scene is as follows:

~~~cpp
    for( auto& Range : Scene.m_Ranges )
    {
        // This would indicate this range there is not sub-range that is free so we need to move to the next one
        if( Range.m_FreeSubrangeMask == 0xffffffffffffffff ) continue;

        // There is some sub-range that is free inside the range 
        const auto FreeSubrangeIndex = getIndexOfFirstZeroBit(Range.m_FreeSubrangeMask);

        // If we were the last entry to fill the subrange we will mark the FreeSubrage Bit as 1
        if( Range.m_SubRanges[FreeSubrangeIndex] == 0xff ) Range.m_FreeSubrangeMask |= (1<<FreeSubrangeIndex);
        else
        {
            // If we do not have any entries yet  we will assume that we have not mapped this sub-range yet. If we had then the Alloc will fail but we don't care.
            if( Range.m_SubRanges[FreeSubrangeIndex] == 0 ) 
            {
                // Have we mapped the virtual sub-range page yet?
                if( !((Range.VirtualMapMask>>FreeSubrangeIndex)&1) ) 
                {
                    // Do the virtual to physical mapping
                    VirtualAlloc( pRange, sizeof(sub_range), ... );

                    // Now we can mark that we hace officially map the sub-range
                    Range.m_VirtualMapMask |= (1<<FreeSubrangeIndex);
                }
            }
            
            // We will add to the count
            Range.m_SubRanges[FreeSubrangeIndex]++;
        } 

        // Now we can get the pointer of our range
        auto pGlobalInfosInSubrange = (global_info*)&((sub_range*)m_FullRange->m_Ptr)[ Range.m_Number * n_subranges_per_range_v + FreeSubrangeIndex ];

        // Loop to find an empty global_info for us to use
        for( int i=0; i<n_entities_in_subrange_v; ++i )
        {
            if( pGlobalInfosInSubrange[i].m_pPool == nullptr )
            {
                // return the global info index
                return (std::size_t(&pGlobalInfosInSubrange[i]) - std::size_t(m_FullRange->m_Ptr)) / sizeof(global_info);
            }
        }

        // it should never reach here, since we should have found our entry in the above code 
        xassert(false);
    }

    // If we get here means that our scene run out of space in the ranges, this means we will need to alloc another range
    // and try again the code above. 
~~~

### Entity to Range to Scene

To achive this we will use an std::unordered_map to map between the range# and the scene#. 
~~~cpp
    IndexOfRange        = static_cast<int>(static_cast<std::size_t>(BaseAddressOfGlobalInfo - AddressEntityGlobalInfo) / (# entity per range)) - (Preallocated Ranges)))

    // Then to create a noise key so that our hash table is spread evenly we will convert our Index To a Key
    IndexToKey(IndexOfRange);
~~~

We will have also a Scene mgr which keeps track of the scenes


## Entity References to "Global" Entities inside an unloaded Scene

If you have an entity that has a reference to another a global entity. But the scene gets unloaded with free-global-entities, if the entity tries to get/find the entity it will create an exception. This exception should be catch and return to the user as an error. 

---

