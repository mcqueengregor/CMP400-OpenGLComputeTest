#include <iostream>
#include "Shader.h"
#include "VAO.h"
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#define ASSERT(x) if (!(x)) __debugbreak();
#define GLCall(x) GLClearError(); x; ASSERT(GLLogCall(#x, __FILE__, __LINE__))

static void GLClearError()
{
	while (glGetError() != GL_NO_ERROR);
}
static bool GLLogCall(const char* func, const char* filename, int line)
{
	while (GLenum error = glGetError())
	{
		std::cout << "ERROR::GL CALL: (" << error << "):" << func << " " << filename << ": " << line << std::endl;
		return false;
	}
	return true;
}

// Initialise GLFW and GLAD:
GLFWwindow* initOpenGL();
void initImGui(GLFWwindow* window);
void processInput(GLFWwindow* window, float dt);
void gui();

bool g_pauseTime = true;

// Raymarching data:
glm::vec3 g_cameraPos(0.0f, 1.0f, 0.0f);
glm::vec4 g_sphereData(0.0f, 1.0f, 6.0f, 1.0f);	// w-component is radius.

glm::vec3 g_boxPos(-2.5f, 1.0f, 6.0f);
glm::vec3 g_boxDim(0.5f);

glm::vec3 g_torusPos(2.0f, 1.0f, 6.0f);
glm::vec2 g_torusRadii(1.0f, 0.2f);				// x-component is circular radius, y-component is ring thickness.
float g_kParam = 1e-2;

// Noise data:
float g_noiseTParam = 0.0f;
float g_noiseFreq = 0.01f;

// LUT data:
glm::vec3 g_wavelengths = glm::vec3(700, 530, 440);
float g_scatterStrength = 1.0f;
float g_tau			= 1.0f;
float g_gParam		= 0.4f;
float g_distance	= 10.0f;

float g_vecLength	= 25.0f;
float g_lightZFar	= 50.0f;
glm::vec3 g_wavelengthDivisor = glm::vec3(1.0);

float g_constant	= 1.0f;
float g_linear		= 0.09f;
float g_quadratic	= 0.032f;

bool g_raymarchOrNoise = false;	// 'false' = raymarching, 'true' = 3D noise.

const int WIDTH = 1024, HEIGHT = 1024, DEPTH = 50;

int main()
{
	GLFWwindow* window = initOpenGL();
	if (!window)
		return -1;

	initImGui(window);

	std::cout << "Hello, world!\n" << glGetString(GL_VERSION) << std::endl;

	Shader rayComputeShader;
	Shader noiseComputeShader;
	Shader fullscreenShader;
	Shader accumLutShader;
	Shader sumLutShader;
	Shader kovalovsLutShader;

	rayComputeShader.loadShader("res/raymarchComputeShader.comp");
	noiseComputeShader.loadShader("res/noise3DComputeShader.comp");
	fullscreenShader.loadShader("res/fullscreenShader_vertex.vert", "res/fullscreenShader_frag.frag");
	accumLutShader.loadShader("res/accumLUTShader.comp");
	sumLutShader.loadShader("res/sumLUTShader.comp");
	kovalovsLutShader.loadShader("res/kovalovsLUTShader.comp");

	fullscreenShader.use();
	fullscreenShader.setInt("u_rayTex", 0);
	fullscreenShader.setInt("u_noiseTex", 1);
	fullscreenShader.setInt("u_lutTex", 2);

#pragma region TextureSetup
	// Create raymarching texture to be written to in compute shader:
	GLuint rayTex;
	glGenTextures(1, &rayTex);
	glBindTexture(GL_TEXTURE_2D, rayTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	// Create 3D noise texture to be written to in compute shader:
	GLuint noiseTex;
	glGenTextures(1, &noiseTex);
	glBindTexture(GL_TEXTURE_3D, noiseTex);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, WIDTH, HEIGHT, DEPTH, 0, GL_RGB, GL_FLOAT, NULL);

	GLuint lutTex;
	glGenTextures(1, &lutTex);
	glBindTexture(GL_TEXTURE_2D, lutTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	GLuint scatterAccumTex;
	glGenTextures(1, &scatterAccumTex);
	glBindTexture(GL_TEXTURE_2D, scatterAccumTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	GLuint summedLutTex;
	glGenTextures(1, &summedLutTex);
	glBindTexture(GL_TEXTURE_2D, summedLutTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
#pragma endregion
#pragma region PrintComputeDetails
	{
		// Print max number of worker groups:
		int workGroupCount[3];
		for (int i = 0; i < 3; ++i)
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &workGroupCount[i]);
		printf("\nMax global (total) work group counts: (%i, %i, %i)\n",
			workGroupCount[0], workGroupCount[1], workGroupCount[2]);

		// Print max size of a worker group:
		int workGroupSize[3];
		for (int i = 0; i < 3; ++i)
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &workGroupSize[i]);
		printf("Max local (in one shader) work group sizes: (%i, %i, %i)\n",
			workGroupSize[0], workGroupSize[1], workGroupSize[2]);

		// Print max number of worker group invocations:
		int workGroupInvocations;
		glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &workGroupInvocations);
		printf("Max local work group invocations: %i\n", workGroupInvocations);
	}
#pragma endregion

	// Bind raymarching texture:
	float fullscreenQuad[] = {
		// (Described as NDC coords)
		// positions   // texCoords
		-1.0f,  1.0f,  0.0f, 1.0f,
		-1.0f, -1.0f,  0.0f, 0.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,

		-1.0f,  1.0f,  0.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,
		 1.0f,  1.0f,  1.0f, 1.0f
	};
	VAO fullscreenVAO(fullscreenQuad, sizeof(fullscreenQuad), VAO::Format::POS2_TEX2);
	
	float dt{}, lastFrame{};

	std::string renderDebugText		= std::string("Rendering");
	std::string rayDebugText		= std::string("Raymarching pass");
	std::string noiseDebugText		= std::string("Noise pass");
	std::string fullscreenDebugText = std::string("Fullscreen quad pass");
	std::string guiDebugText		= std::string("GUI pass");

	float time{};

	while (!glfwWindowShouldClose(window))
	{
		// Start new ImGui frame:
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();

		float currentFrame = glfwGetTime();
		dt = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// Toggle passing of time (pauses light movement and noise scrolling):
		if (!g_pauseTime)
			time += dt;

		processInput(window, dt);

		glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Rendering debug group:
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, renderDebugText.size(), renderDebugText.c_str());
		{
			// Raymarching debug group:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, rayDebugText.size(), rayDebugText.c_str());
			{
				// Run raymarching compute shader:
				rayComputeShader.use();
				rayComputeShader.setVec3("u_cameraPos", g_cameraPos);
				rayComputeShader.setVec4("u_sphereData", g_sphereData);
				rayComputeShader.setVec3("u_boxPos", g_boxPos);
				rayComputeShader.setVec3("u_boxDim", g_boxDim);
				rayComputeShader.setVec3("u_torusPos", g_torusPos);
				rayComputeShader.setVec2("u_torusDim", g_torusRadii);
				rayComputeShader.setFloat("u_time", time);
				rayComputeShader.setFloat("u_kParam", g_kParam);

				glBindImageTexture(0, rayTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
				glDispatchCompute(1, HEIGHT, 1);
			}
			glPopDebugGroup();

			 //3D noise debug group:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, noiseDebugText.size(), noiseDebugText.c_str());
			{
				// Run noise compute shader:
				noiseComputeShader.use();
				noiseComputeShader.setFloat("u_freq", g_noiseFreq);
				noiseComputeShader.setFloat("u_time", time);

				glBindImageTexture(1, noiseTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
				glDispatchCompute(1, HEIGHT, DEPTH);
			}
			glPopDebugGroup();

			// Hoobler LUT shader stuffs:
			{
				// Calculate scattering coefficients based on wavelengths:
				glm::vec3 scatteringCoefficients = glm::vec3(g_scatterStrength);
				scatteringCoefficients.x *= powf(g_wavelengthDivisor.x / g_wavelengths.x, 4);
				scatteringCoefficients.y *= powf(g_wavelengthDivisor.y / g_wavelengths.y, 4);
				scatteringCoefficients.z *= powf(g_wavelengthDivisor.z / g_wavelengths.z, 4);

				accumLutShader.use();
				accumLutShader.setVec3("u_scatteringCoefficients", scatteringCoefficients);
				accumLutShader.setFloat("u_tau", g_tau);
				accumLutShader.setFloat("u_distance", g_distance);
				accumLutShader.setFloat("u_gParam", g_gParam);

				accumLutShader.setFloat("u_vecLength", g_vecLength);
				accumLutShader.setFloat("u_lightZFar", g_lightZFar);

				accumLutShader.setFloat("u_constant", g_constant);
				accumLutShader.setFloat("u_linear", g_linear);
				accumLutShader.setFloat("u_quadratic", g_quadratic);

				glBindImageTexture(3, scatterAccumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(4, lutTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(5, summedLutTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				//glDispatchCompute(1, HEIGHT, 1);

				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

				sumLutShader.use();
				//glDispatchCompute(1, 1, 1);
			}

			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			// Kovalovs LUT shader stuffs:
			{
				kovalovsLutShader.use();
				kovalovsLutShader.setFloat("u_gParam", g_gParam);

				kovalovsLutShader.setFloat("u_constant", g_constant);
				kovalovsLutShader.setFloat("u_linear", g_linear);
				kovalovsLutShader.setFloat("u_quadratic", g_quadratic);

				glBindImageTexture(6, lutTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glDispatchCompute(1, HEIGHT, 1);
			}

			// Block until compute operations have been completed:
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			// Take outputted textures and display on-screen:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, fullscreenDebugText.size(), fullscreenDebugText.c_str());
			{
				fullscreenShader.use();
				fullscreenShader.setFloat("u_tParam", g_noiseTParam);
				fullscreenShader.setBool("u_renderMode", g_raymarchOrNoise);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, lutTex);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_3D, noiseTex);
				fullscreenVAO.bind();
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}
			glPopDebugGroup();
		}
		glPopDebugGroup();

		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, guiDebugText.size(), guiDebugText.c_str());
		{
			gui();
		}
		glPopDebugGroup();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	// Shutdown ImGui:
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwTerminate();
	return 0;
}

GLFWwindow* initOpenGL()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* newWindow = glfwCreateWindow(WIDTH, HEIGHT, "ComputeShaderTest", NULL, NULL);

	if (!newWindow)
	{
		std::cout << "Failed to create GLFW window." << std::endl;
		glfwTerminate();
		return nullptr;
	}
	glfwMakeContextCurrent(newWindow);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialise GLAD." << std::endl;
		return nullptr;
	}
	glViewport(0, 0, WIDTH, HEIGHT);
	return newWindow;
}

void initImGui(GLFWwindow* window)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");
}

void processInput(GLFWwindow* window, float dt)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE))
		glfwSetWindowShouldClose(window, true);

	const float speed = 3.0f * dt;

	// Raymarching camera movement:
	if (glfwGetKey(window, GLFW_KEY_W))
		g_cameraPos.z += speed;
	if (glfwGetKey(window, GLFW_KEY_A))
		g_cameraPos.x -= speed;
	if (glfwGetKey(window, GLFW_KEY_S))
		g_cameraPos.z -= speed;
	if (glfwGetKey(window, GLFW_KEY_D))
		g_cameraPos.x += speed;
	if (glfwGetKey(window, GLFW_KEY_R))
		g_cameraPos.y += speed;
	if (glfwGetKey(window, GLFW_KEY_F))
		g_cameraPos.y -= speed;
}

void gui()
{
	ImGui::Begin("ImGui");
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::Text("Camera position: (%f, %f, %f)", g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
	ImGui::Checkbox("Raymarching or 3D noise", &g_raymarchOrNoise);
	ImGui::Checkbox("Pause time", &g_pauseTime);

	// LUT data controls:
	if (ImGui::CollapsingHeader("LUT Shader controls"))
	{
		ImGui::Text("Calculation variables:");
		ImGui::DragFloat3("Wavelengths", &g_wavelengths.x, 0.5f, 0.0f, 700.0f);
		ImGui::SliderFloat("Scattering strength", &g_scatterStrength, 0.0f, 50.0f);
		ImGui::SliderFloat("G parameter (phase)", &g_gParam, -1.0f + 0.001f, 1.0f - 0.001f);

		ImGui::Text("Hoobler data:");
		ImGui::SliderFloat("vecLength", &g_vecLength, 0.0f, 50.0f);
		ImGui::SliderFloat("lightZFar", &g_lightZFar, 0.0f, 50.0f);
		ImGui::DragFloat3("Wavelength divisors", &g_wavelengthDivisor.x, 1.0f, 1.0f, 400.0f);

		ImGui::Text("Light data:");
		ImGui::SliderFloat("Light constant", &g_constant, 0.0f, 1.0f);
		ImGui::SliderFloat("Light linear", &g_linear, 0.0f, 0.5f);
		ImGui::SliderFloat("Light quadratic", &g_quadratic, 0.0f, 0.1f);
	}

	// Raymarcher controls:
	if (!g_raymarchOrNoise) 
	{
		if (ImGui::CollapsingHeader("Raymarcher controls"))
		{
			ImGui::Text("Sphere:");
			ImGui::SliderFloat3("Sphere position", &g_sphereData.x, -10.0f, 10.0f);
			ImGui::SliderFloat("Sphere radius", &g_sphereData.w, 0.001f, 3.0f);

			ImGui::Text("Box:");
			ImGui::SliderFloat3("Box position", &g_boxPos.x, -10.0f, 10.0f);
			ImGui::SliderFloat3("Box dimensions", &g_boxDim.x, 0.001f, 3.0f);
			
			ImGui::Text("Torus:");
			ImGui::SliderFloat3("Torus position", &g_torusPos.x, -10.0f, 10.0f);
			ImGui::SliderFloat2("Torus radii", &g_torusRadii.x, 0.001f, 3.0f);

			ImGui::Text("");
			ImGui::SliderFloat("Smooth minimum", &g_kParam, 0.001f, 3.0f);
		}
	}
	// 3D noise controls:
	else
	{
		if (ImGui::CollapsingHeader("Noise controls"))
		{
			ImGui::SliderFloat("Noise frequency", &g_noiseFreq, 0.001f, 0.5f);
			ImGui::SliderFloat("Noise depth slice", &g_noiseTParam, 0.0f, 1.0f);
		}
	}

	ImGui::End();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}