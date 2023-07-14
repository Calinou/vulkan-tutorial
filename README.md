# Vulkan Tutorial

My assignment code for [vulkan-tutorial.com](https://vulkan-tutorial.com/).

## Compiling

### Dependencies

- [Meson](https://mesonbuild.com/)
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)
- [GLFW](https://www.glfw.org/)
- [glm](https://github.com/g-truc/glm)
- [glslc](https://github.com/google/shaderc)

### Building

```shell
cd hello_triangle
meson setup build
meson compile -C build
build/hello_triangle
```

Shaders are compiled automatically when needed.

## License

Copyright © 2023-present Hugo Locurcio and contributors

Unless otherwise specified, files in this repository are licensed under the
MIT license. See [LICENSE.md](LICENSE.md) for more information.
