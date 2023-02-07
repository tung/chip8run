@vs vs
in vec2 position;
in vec2 a_tex_coord;

out vec2 tex_coord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    tex_coord = a_tex_coord;
}
@end

@fs fs
in vec2 tex_coord;

out vec4 frag_color;

uniform sampler2D tex;

void main() {
    float r = texture(tex, tex_coord).r;
    frag_color = vec4(r, r, r, 1.0);
}
@end

@program simple vs fs
