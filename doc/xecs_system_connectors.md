# System connectors

A system can have **connectors**: named places where other systems are connected. The connected systems are its children; the parent decides when they
run and how many times, and they run in the order they have in the system manager.

## In code (the parent)

```cpp
struct physics_system : xecs::system::instance
{
    static constexpr std::array<xecs::system::connector, 2> connectors_v
    { { { "Before Step", "Runs once for every fixed step, right before the world takes it ..." }
      , { "After Step",  "Runs once for every fixed step, right after ..." } } };

    void OnUpdate() noexcept
    {
        for (int i = 0; i < Steps; ++i) { RunConnector(0); /* the step */ RunConnector(1); }
    }
};
```

The name and the description are what the person building the hierarchy in the editor sees. A system can have several connectors.

## In data (who is connected to what)

Nothing in code says which system is the child of which: that is data, saved with the order of the update systems in
`Project.config\SystemOrder.config.txt` (`ParentGuid` and the `Connector` name on each entry). A system with no data runs at the top level, in the order it
was registered. A saved parent that is gone, a connector it does not have any more or a loop leaves the system at the top level.

A system that is meant to be a child can ask `isConnected()` and do one step when it is, or the work of the whole frame when it is not.

## Editing it

- The **System Registry** panel shows the tree: the connectors of a system are rows under it (hover for the description), the children are indented under
  their connector. Drag a system onto a connector to connect it, onto another system to take its place next to it, onto the drop zone at the bottom to run
  it at the top level again. Edits are saved at once (and thrown away on Stop, like the rest of the order).
- The pipe has `Name\ListSystems` (it ends with the hierarchy), `Name\SetSystemParent -System name [-Parent name -Connector name]` and
  `Name\SaveSystemOrder`.
- In code: `xecs::system::mgr::SetUpdateSystemParent`, `GetUpdateSystemRows` (the rows carry the parent and the connector), `GetConnectors`.

Only update systems have connectors and can be connected.
