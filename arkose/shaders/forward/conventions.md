Conventions & rules for forward shaders
=======================================

These forward shaders are used by multiple passes, for example ForwardRenderNode, TranslucentNode, and MeshletForwardRenderNode, so we need to be consistent in how we assign the binding sets. Here are some brief rules/conventions to stick to:

Binding set...

 0. the camera binding set
 1. reserved for **task shader** bindings
 2. reserved for **vertex** *or* **mesh shader** bindings
 3. (and everything after 3) free to use binding sets for the **fragment shader**

Note that within a pass you might be aware of the binding sets for the other stages, and you may make use of these. For example, in a mesh shader pass you can of course use the task shader bindings in the mesh shader stage.
