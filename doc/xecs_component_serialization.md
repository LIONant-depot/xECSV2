<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Component](xecs_component.md) / Serialization

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_typedef_serialization.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Serializing components is a deceitfully tricky operation, here are some of the reasons why:

* Needs to be easy to use for the end-user
* Has to provide forward and backwards compatibility
* It should allow the Scene Loader to skip deleted components
* It needs to be debugable (a nice text format to inspect), yet remain performant 
* It also needs to be relatively fast. A binary file must also be supported

xECS supports a few ways to save the components:

1. By properties (This is the default way)
2. By serialize function. This provides a much faster way to serialize your components and can be used for simple components.
3. By a full serialize function. Also a very fast way to serialize components but this version allows to deal with complex components as well.


## Scene entity-id remapping 

When serializing entities in Scenes we need to remap any reference to other entities that they the component may have. This remapping is not just a requirement for serialization but also when creating Scene-prefabs. So we encourage the users to be aware of the different ways to do this:

| User provides | Serialization Speed | Scene-Prefab Speed | Comments |
|:-------------:|:-------------------:|:------------------:|----------|
|  Nothing      |         X           |         X          | It won't serialize anything or Instantiate any prefabs |
|  Properties   |        BAD          |        BAD         | Remapping will happen automatically via the properties. This option is OK but is the slowest method, and typically done when performance is not an issue yet.  |
|  Serialization Function | GOOD      |        BAD         | Remapping will happen automatically via the the serialization function. For Serialization this is very fast but has the problem of decaying to the property version when creating Scene-Prefabs.
| ReportReferenceFn |   GOOD   | GOOD | ReportReferenceFn is in the typedef_v. This is the fastest way across the board. This removes any worry on performance when creating Scene-Prefabs. |

Here is an example on how to provide the ReportReferenceFn:
~~~cpp

namespace details
{
    void ReportEntites( std::vector<xecs::component::entity*>& , std::byte* ) noexcept;
}

struct hierarchy
{
    // Sets our function as the m_pReportReferencesFn method, this will let the system knows to call us when remapping is necessary
    static constexpr auto typedef_v = xecs::component::type::data
    {
        .m_pReportReferencesFn  = details::ReportEntites
    };

    xecs::component::entity              m_Parent;          // Who is our parent entity?
    std::vector<xecs::component::entity> m_Children;        // A vector with all our children entities 
};

void details::ReportEntites( std::vector<xecs::component::entity*>& Entities, std::byte* pComponent ) noexcept
{
    auto& Hierarchy = *reinterpret_cast<hierarchy*>(pComponent);
    Entities.push_back( &Hierarchy.m_Parent );
    for( auto& E : Hierarchy.m_Children ) Entities.push_back( &E );
}
~~~

## Serializing via Properties (Default)

Properties are a very important concept in game development, and it is used for different type of functionality:

1. Create prefabs
2. Edit the component
3. Multi-Select Edits of components
4. Copy and paste components
5. Scripting functionality
6. Serializing 

Here is simple example on how to use it:
~~~cpp
struct transform
{
    static constexpr auto typedef_v = xecs::component::type::data{};

    xcore::quaternion   m_Rotation;
    xcore::vector3      m_Position;
    xcore::vector3      m_Scale;
};

// Here are the properties for our transform component
property_begin( transform )
{
      property_var( m_Position )
    , property_var( m_Rotation  )
    , property_var( m_Scale  )
} property_end_h()
~~~

It can get allot more complex depending on what you are doing, but simple components don't require allot.

## Serialize function (Advance Topic)

Like mention serialize function is not very complex to do and does provide extra speed for the components. Please note that the serialization function only gets used to serialize the component and nothing else. Having properties still a requirement for many of the other features.

Here is an example on how to serialize the above component.

<details><summary><i><b>Example </b>(Click to open)</i></summary>

~~~cpp
namespace details
{
    // Pre-defines the serialization function, this function needs to have this exact signature
    xcore::err Serialize( xcore::textfile::stream&, bool, std::byte* ) noexcept;
}

struct transform
{
    // Sets our function as the serialization method. By putting it here the system will know to use it and skip the properties.
    static constexpr auto typedef_v = xecs::component::type::data
    {
        .m_pSerilizeFn  = details::Serialize
    };

    xcore::quaternion   m_Rotation;
    xcore::vector3      m_Position;
    xcore::vector3      m_Scale;
};

// Serialize function
xcore::err details::Serialize           // Note that the function returns an error code
( xcore::textfile::stream&  TextFile    // xcore provides the serializer, so this is the instance to it
, bool                   // isRead      // Variable that tells you if we are reading or writing (This example does not use it)
, std::byte*                pComponent  // Generic Pointer to the component
) noexcept                              // Note that the function is marked as no throwing exceptions 
{
    auto&       Transform = *reinterpret_cast<transform*>(pComponent); // Converts the byte pointer to our component
    xcore::err  Error;                                                 // Variable to store the error if any

    // Each of the function will read/write the component data and if there is an error it will store it in the Error 
    // variable and immediately stop reading or writing
       (Error = TextFile.Field( "Position", Transform.m_Position.m_X, Transform.m_Position.m_Y. Transform.m_Position.m_Z ))
    || (Error = TextFile.Field( "Rotation", Transform.m_Rotation.m_X, Transform.m_Rotation.m_Y. Transform.m_Rotation.m_Z, Transform.m_Rotation.m_W ))
    || (Error = TextFile.Field( "Scale",    Transform.m_Scale.m_X,    Transform.m_Scale.m_Y.    Transform.m_Scale.m_Z ))

    // Returns the error if any
    return Error;
}
~~~
</details>

## Full Serialize function (Very Advance Topic)

When you want to serialize complex components quickly then you will need this method. This allow you to save components that have vectors and other types of complex containers. That been said this is a more complex topic and requires semi-advance users:

<details><summary><i><b>Example </b>(Click to open)</i></summary>

~~~cpp
namespace details
{
    // Pre-defines the serialization function, this function need sto have this exact signature
    xcore::err FullSerialize( xcore::textfile::stream&, bool, std::byte*, int& ) noexcept;
}

struct hierarchy
{
    // Sets our function as the serialization method. By putting it here the system will know to use it and skip the properties.
    static constexpr auto typedef_v = xecs::component::type::data
    {
        .m_pFullSerializeFn  = details::FullSerialize
    };

    xecs::component::entity              m_Parent;          // Who is our parent entity?
    std::vector<xecs::component::entity> m_Children;        // A vector with all our children entities 
};

// Serialize function
xcore::err details::FullSerialize       // Note that the function returns an error code
( xcore::textfile::stream&  TextFile    // [IN] xcore provides the serializer, so this is the instance to it
, bool                      isRead      // [IN] Variable that tells you if we are reading or writing (This example does not use it)
, std::byte*                pComponentArray // [IN/OUT] Pointer to the begging of the array of our components
, int&                      Count       // [IN/OUT] When writing it tells us how many entities we must write, when reading we must return how many we read
) noexcept                              // Note that the function is marked as no throwing exceptions 
{
    auto&       pHierarchyArray = *reinterpret_cast<hierarchy*>(pComponentArray); // Converts the byte pointer to our array of components
    xcore::err  Error;                                                            // Variable to store the error if any

    //
    // This first block of data we will serialize all the component information:
    // which for the moment is the parent and the number of children.
    // All the children will be serialize together as a separate table
    //
    int TotalChildren=0;    // We want to know the total number of children form all the components together
    if( TextFile.Record     // This record function will return true if it found an error
    ( Error                 // We pass the error variable to get the actual error this variable gets used in the callbacks too
    , "Hierarchy"           // Name of this record
    , [&]                   // This is our callback to deal with how many entries in our table
    ( std::size_t& Size     // [IN/OUT] [In] how many element we are reading, [Out] How many we are writing
    , xcore::err&           // [OUT] There won't be any errors to report
    ) noexcept    
    {
        // If we are reading then we will need to report to the caller of FullSerialize how many entries we got
        if( isRead ) Count = static_cast<int>(Size);
        else         Size  = Count;      // If we are writing then we need to tell the text file how many entries we will write
    }
    ,[&]                    // This callback deals with each entry (component)
    ( std::size_t I         // [In]  I is the current entry index
    , xcore::err& Err       // [Out] If we had an error we may need to report it
    ) noexcept 
    {
        // Get our entry I
        auto& Hierarchy = pHierarchyArray[I];    

        // We need to convert the count into a variable which we can read and write to it
        // If we are reading we don't know yet the value so just set zero
        int   nChildren = isRead ? 0 : static_cast<int>(Hierarchy.m_Children.count());

        // We serialize the Parent and The number of children
           (Err = TextFile.Field( "Parent",    Hierarchy.m_Parent.m_Value ))
        || (Err = TextFile.Field( "nChildren", nChildren ));

        // If we are reading then we must set our vector to be the size of whatever we just read
        if(isRead) Hierarchy.m_Children.resize(nChildren);

        // We need to know how many entries we will have in total when we write/read out children later
        TotalChildren += nChildren;

    }) ) return Error; // IF the Record has any error the if statement will be true and we must return the error

    //
    // We are going to serialize all the children together now. xcore::textile forces us to serialize
    // using tables this allow for high speed serialization.
    //
    int         CurrentChild      = 0;                  // This is the current children index that we are serializing for a given Hierarchy
    hierarchy*  pCurrentHierarchy = pHierarchyArray;    // This is our current Hierarchy    
    if( TotalChildren       // If we have any children to read/write then do this table other wise skip it
     && TextFile.Record     // If we are doing the record if it returns true then we have an error
    ( Error                         // The error variable    
    , "HierarchyChildren"           // The name of this table
    , [&]                                   // Our callback to deal with the size of this table
    ( std::size_t& Size         // [In/Out] [In] How many entries we are reading, [Out] How many entries we will write
    , xcore::err&               // [Out] The won't be any errors to report
    ) noexcept
    {

        // When reading The TotalChildren should be the same size as the amount of entries in this table
        if( isRead ) assert( TotalChildren == static_cast<int>(Size) ); 
        else         Size  = static_cast<std::size_t>(TotalChildren);     // For writing we set the size to be the count of all the children
    }
    ,[&]                        // Our callback to deal for each child
    ( std::size_t                   // [IN] This is the current index of the total number of children. We don't need this.
    , xcore::err& Err               // [OUT] For reporting the errors
    ) noexcept
    {
        // Check if Hierarchy has no children, if so loop and get the next Hierarchy OR
        // We may be finish writing all the children for a particular Hierarchy if that is the case get the next Hierarchy
        while( pCurrentHierarchy->m_Children.count() >= CurrentChild )
        {
            pCurrentHierarchy++;    // Move to the next Hierarchy
            CurrentChild = 0;       // Always reset the index of the child when we move to the next Hierarchy
        }

        // Read/Write the child info, and move on to the next Child
        Err = TextFile.Field( "Children", pCurrentHierarchy->m_Children[CurrentChild++].m_Value );
        
    }) ) return Error;
}
~~~
</details>


## Backwards compatibility with Properties

TODO:

## Baclwards compatibility with Serialization Functions

TODO:

---