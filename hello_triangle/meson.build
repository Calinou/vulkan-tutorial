project('hello_triangle', 'cpp', default_options: ['cpp_std=c++20'])

glfw3_dep = dependency('glfw3')
glm_dep = dependency('glm')
vulkan_headers_dep = dependency('vulkan')

glslc = find_program('glslc')
shader_target = custom_target(
    'shaders',
    input: ['shader.vert', 'shader.frag'],
    output: ['shader.vert.spv', 'shader.frag.spv'],
    command: [glslc, '-Werror', '-c', '@INPUT@'],
    install: true,
    install_dir: '/',
)

executable('hello_triangle', 'main.cpp', dependencies: [glfw3_dep, glm_dep, vulkan_headers_dep])
