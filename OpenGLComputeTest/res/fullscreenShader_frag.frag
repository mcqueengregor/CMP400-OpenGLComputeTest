#version 430 core
out vec4 FragColour;

in vec2 TexCoords;

// Texture samplers:
uniform sampler2D u_rayTex;
uniform sampler3D u_noiseTex;

uniform int u_renderMode;
uniform float u_tParam;

void main()
{
	switch (u_renderMode)
	{
	case 0:
		FragColour = texture(u_rayTex, TexCoords);
		break;
	case 1:
		FragColour = texture(u_noiseTex, vec3(TexCoords, u_tParam));
		break;
	}
}