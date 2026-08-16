# Scene Components Submodule (`coopa::scene::components`)

The `components` submodule contains concrete `Component` subclasses attached to `SceneObject` nodes to define transform hierarchy, visual representation, lighting, cameras, and global illumination parameters.

---

## Component Inheritance Graph

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                            COMPONENT INHERITANCE HIERARCHY                               │
└──────────────────────────────────────────────────────────────────────────────────────────┘
                                           │
                                ┌──────────┴──────────┐
                                │      Component      │
                                │     (Abstract)      │
                                └──────────┬──────────┘
                                           │
         ┌───────────────┬─────────────────┼─────────────────┬───────────────┐
         │               │                 │                 │               │
         ▼               ▼                 ▼                 ▼               ▼
 ┌───────────────┐ ┌───────────────┐ ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
 │   Transform   │ │ MeshRenderer  │ │    Camera     │ │ Directional   │ │  PointLight   │
 │   Component   │ │   Component   │ │   Component   │ │     Light     │ │   Component   │
 ├───────────────┤ ├───────────────┤ ├───────────────┤ ├───────────────┤ ├───────────────┤
 │ position      │ │ PbrMaterial   │ │ CameraType    │ │ color         │ │ color         │
 │ rotation      │ │ GPU Mesh ptr  │ │ fov / ortho   │ │ intensity     │ │ intensity     │
 │ scale         │ │ is_ready()    │ │ clip planes   │ │ direction     │ │ range         │
 └───────────────┘ └───────────────┘ └───────────────┘ └───────────────┘ └───────────────┘
                                           │
                         ┌─────────────────┼─────────────────┐
                         ▼                                   ▼
                 ┌───────────────┐                   ┌───────────────┐
                 │  Environment  │                   │ GiProbeVolume │
                 │     Light     │                   │   Component   │
                 ├───────────────┤                   ├───────────────┤
                 │ color         │                   │ grid_resolution│
                 │ intensity     │                   │ origin, dims  │
                 └───────────────┘                   └───────────────┘
```

---

## Component Breakdown

### [`transform_component.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/transform_component.h)

Manages 3D position, rotation (Euler angles in degrees), and scale:
- `position`: Local position (`glm::vec3`).
- `rotation`: Local Euler angles in degrees (`glm::vec3` XYZ pitch/yaw/roll).
- `scale`: Local scale factors (`glm::vec3`).
- `get_local_matrix() -> glm::mat4`: Computes $T \times R \times S$ transformation matrix.
- `get_world_matrix() -> glm::mat4`: Recursively combines parent world transforms with local matrix down the scene tree.

### [`mesh_renderer.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/mesh_renderer.h)

Holds mesh geometry references and PBR material specifications:
- `PbrMaterial`:
  - `albedo` (`glm::vec3`, default `[0.8, 0.8, 0.8]`): Surface diffuse color.
  - `metallic` (`float`, default `0.0`): Metallic factor $[0, 1]$.
  - `roughness` (`float`, default `0.5`): Roughness factor $[0, 1]$.
  - `ao` (`float`, default `1.0`): Ambient occlusion factor.
- `mesh`: Shared pointer to a GPU `coopa::gfx::engine::Mesh`.
- `set_mesh(...)`: Assigns GPU mesh asset.

### [`camera_component.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/camera_component.h)

Defines perspective or orthographic view projections:
- `type`: `CameraType::Perspective` or `CameraType::Orthographic`.
- `fov`: Vertical field of view in degrees (Perspective mode).
- `orthographic_size`: Orthographic half-height in world units (Orthographic mode).
- `clip_start` / `clip_end`: Near and far clip distances.
- `get_view_matrix() -> glm::mat4`: Inverts the owner object's world matrix.
- `get_projection_matrix(aspect) -> glm::mat4`: Generates perspective or orthographic projection.

### [`directional_light.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/directional_light.h)

Represents sunlight or infinite directional illumination:
- `color`: Light RGB color tint (`glm::vec3`).
- `intensity`: Lux / brightness multiplier (`float`).
- `direction`: Normalized world-space illumination vector (`glm::vec3`).
- `cast_shadows`: Enables directional shadow map generation (`bool`).

### [`point_light.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/point_light.h)

Represents omnidirectional local point lights:
- `color`: Light RGB color (`glm::vec3`).
- `intensity`: Candela / brightness multiplier (`float`).
- `range`: Maximum attenuation sphere radius (`float`).
- `attenuation_constant`, `attenuation_linear`, `attenuation_quadratic`: Distance falloff factors.
- `cast_shadows`: Enables 6-face cube shadow map generation (`bool`).

### [`environment_light.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/environment_light.h)

Represents global ambient background lighting:
- `color`: Sky / ambient color tint (`glm::vec3`).
- `intensity`: Ambient strength multiplier (`float`).

### [`gi_probe_volume.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/gi_probe_volume.h)

Defines a 3D grid volume for Spherical Harmonics indirect GI baking:
- `grid_resolution`: Grid counts along X, Y, Z (`glm::ivec3`, e.g. $10 \times 10 \times 5$).
- `origin`: Minimum bounding corner in world space (`glm::vec3`).
- `dimensions`: Total size extents of the volume (`glm::vec3`).
- `gi_intensity`: Scaling multiplier applied to baked indirect lighting.
- `total_probes() -> int`: Returns total probe count ($N_x \times N_y \times N_z$).
- `probe_position(ix, iy, iz) -> glm::vec3`: Returns world position of grid probe $(ix, iy, iz)$.

### [`reflection_probe.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/reflection_probe.h)

Defines a local specular reflection cubemap volume:
- `blend_distance`: Edge transition distance for probe blending (`float`).
- `importance`: Priority rank when overlapping multiple reflection probes (`int`).
- `get_box_min() / get_box_max()`: Computes local bounding box bounds for parallax-corrected reflections.
