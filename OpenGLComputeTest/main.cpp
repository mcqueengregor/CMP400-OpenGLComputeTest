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

bool g_KorH = false;			// 'false' = output Kovalovs' LUT, 'true' = output Hoobler's LUT.
bool g_accumOrSum = false;		// 'false' = output accum LUT, 'true' = output summed LUT.

const int WIDTH = 1024, HEIGHT = 1024, DEPTH = 50;

int main()
{
	GLFWwindow* window = initOpenGL();
	if (!window)
		return -1;

	initImGui(window);

	std::cout << "Hello, world!\n" << glGetString(GL_VERSION) << std::endl;

	Shader fullscreenShader;
	Shader hooblerAccumLutShader;
	Shader hooblerSumLutShader;
	Shader kovalovsLutShader;

	fullscreenShader.loadShader("res/fullscreenShader_vertex.vert", "res/fullscreenShader_frag.frag");
	hooblerAccumLutShader.loadShader("res/hooblerAccumLUTShader.comp");
	hooblerSumLutShader.loadShader("res/hooblerSumLUTShader.comp");
	kovalovsLutShader.loadShader("res/kovalovsLUTShader.comp");

	fullscreenShader.use();
	fullscreenShader.setInt("u_lutTex", 0);

#pragma region TextureSetup
	// Final output of Hoobler's LUT calculations:
	GLuint hooblerAccumLutTex;
	glGenTextures(1, &hooblerAccumLutTex);
	glBindTexture(GL_TEXTURE_2D, hooblerAccumLutTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	// Final output of Kovalovs' LUT calculations:
	GLuint kovalovsLutTex;
	glGenTextures(1, &kovalovsLutTex);
	glBindTexture(GL_TEXTURE_2D, kovalovsLutTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, WIDTH, HEIGHT, 0, GL_RED, GL_FLOAT, NULL);

	// Intermediate buffer used in Hoobler's LUT calculations:
	GLuint scatterAccumTex;
	glGenTextures(1, &scatterAccumTex);
	glBindTexture(GL_TEXTURE_2D, scatterAccumTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	// Results after sum pass for Hoobler's LUT calculations:
	GLuint hooblerSummedLutTex;
	glGenTextures(1, &hooblerSummedLutTex);
	glBindTexture(GL_TEXTURE_2D, hooblerSummedLutTex);
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
	std::string hooblerDebugText	= std::string("Hoobler LUT pass");
	std::string kovalovsDebugText	= std::string("Kovalovs LUT pass");
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

		processInput(window, dt);

		glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Rendering debug group:
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, renderDebugText.size(), renderDebugText.c_str());
		{
			// Hoobler LUT shader stuffs:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, hooblerDebugText.size(), hooblerDebugText.c_str());
			{
				// Calculate scattering coefficients based on wavelengths:
				glm::vec3 scatteringCoefficients = glm::vec3(g_scatterStrength);
				scatteringCoefficients.x *= powf(g_wavelengthDivisor.x / g_wavelengths.x, 4);
				scatteringCoefficients.y *= powf(g_wavelengthDivisor.y / g_wavelengths.y, 4);
				scatteringCoefficients.z *= powf(g_wavelengthDivisor.z / g_wavelengths.z, 4);

				hooblerAccumLutShader.use();
				hooblerAccumLutShader.setVec3("u_scatteringCoefficients", scatteringCoefficients);
				hooblerAccumLutShader.setFloat("u_tau", g_tau);
				hooblerAccumLutShader.setFloat("u_distance", g_distance);
				hooblerAccumLutShader.setFloat("u_gParam", g_gParam);

				hooblerAccumLutShader.setFloat("u_vecLength", g_vecLength);
				hooblerAccumLutShader.setFloat("u_lightZFar", g_lightZFar);

				hooblerAccumLutShader.setFloat("u_constant", g_constant);
				hooblerAccumLutShader.setFloat("u_linear", g_linear);
				hooblerAccumLutShader.setFloat("u_quadratic", g_quadratic);

				glBindImageTexture(3, scatterAccumTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(4, hooblerAccumLutTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(5, hooblerSummedLutTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glDispatchCompute(32, 128, 1);

				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

				hooblerSumLutShader.use();
				glDispatchCompute(32, 256, 1);
			}
			glPopDebugGroup();

			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			// Kovalovs LUT shader stuffs:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, kovalovsDebugText.size(), kovalovsDebugText.c_str());
			{
				kovalovsLutShader.use();
				kovalovsLutShader.setFloat("u_gParam", g_gParam);

				kovalovsLutShader.setFloat("u_constant", g_constant);
				kovalovsLutShader.setFloat("u_linear", g_linear);
				kovalovsLutShader.setFloat("u_quadratic", g_quadratic);

				glBindImageTexture(6, kovalovsLutTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
				glDispatchCompute(1, HEIGHT, 1);
			}
			glPopDebugGroup();

			// Block until compute operations have been completed:
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			// Take outputted textures and display on-screen:
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, fullscreenDebugText.size(), fullscreenDebugText.c_str());
			{
				fullscreenShader.use();
				glActiveTexture(GL_TEXTURE0);
				g_KorH ? g_accumOrSum ? glBindTexture(GL_TEXTURE_2D, hooblerAccumLutTex)
									  : glBindTexture(GL_TEXTURE_2D, hooblerSummedLutTex)
					   : glBindTexture(GL_TEXTURE_2D, kovalovsLutTex);
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

}

void gui()
{
	ImGui::Begin("ImGui");
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::Checkbox("Kovalovs or Hoobler", &g_KorH);

	if (g_KorH)
		ImGui::Checkbox("Accumulated or summed?", &g_accumOrSum);

	// LUT data controls:
	ImGui::Text("");
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

	ImGui::End();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}