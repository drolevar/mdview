# Mermaid broken-block isolation

Valid:

```mermaid
flowchart TD
    A --> B
```

Syntactically broken:

```mermaid
this is not mermaid syntax at all
   ?>><
```

Unsupported diagram type:

```mermaid
nonexistentDiagramType
   foo --> bar
```

A paragraph after the failures should still render normally.
