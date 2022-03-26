#version 430 core
out vec4 FragColour;

in vec2 TexCoords;

// Texture samplers:
uniform sampler2D u_lutTex;

void main()
{
	FragColour = texture(u_lutTex, TexCoords);
}