#pragma once
#ifndef SHADER_H
#define SHADER_H

#include <glad4.3/glad4.3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader
{
public:
	unsigned int m_ID{};	// Program ID

	Shader() {};
	Shader(const char* computePath);
	Shader(const char* vertexPath, const char* fragmentPath);
	Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath);
	
	void use() const;
	void loadShader(const char* computePath);
	void loadShader(const char* vertexPath, const char* fragmentPath);
	void loadShader(const char* vertexPath, const char* fragmentPath, const char* geometryPath);

	void setBool(const std::string& name, bool val) const;
	void setInt(const std::string& name, int val) const;
	void setFloat(const std::string& name, float val) const;
	void setVec2(const std::string& name, glm::vec2 val) const;
	void setVec2(const std::string& name, float x, float y) const;
	void setVec3(const std::string& name, glm::vec3 val) const;
	void setVec3(const std::string& name, float x, float y, float z) const;
	void setVec4(const std::string& name, glm::vec4 val) const;
	void setVec4(const std::string& name, float x, float y, float z, float w) const;
	void setMat4(const std::string& name, glm::mat4 val) const;

private:
	unsigned int setupStage(const char* path, unsigned int type);
};

#endif // !SHDAER_H