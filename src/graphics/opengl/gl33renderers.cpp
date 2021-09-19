#include "graphics/opengl/gl33renderers.h"

#include "graphics/opengl/gl33device.h"
#include "graphics/opengl/glutil.h"

#include "graphics/core/vertex.h"

#include "common/logger.h"

#include <GL/glew.h>

#include <glm/gtc/type_ptr.hpp>

namespace Gfx
{

CGL33UIRenderer::CGL33UIRenderer(CGL33Device* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CGL33UIRenderer\n");

    GLint shaders[2] = {};

    shaders[0] = LoadShader(GL_VERTEX_SHADER, "shaders/gl33/ui_vs.glsl");
    if (shaders[0] == 0)
    {
        GetLogger()->Error("Cound not create vertex shader from file 'ui_vs.glsl'\n");
        return;
    }

    shaders[1] = LoadShader(GL_FRAGMENT_SHADER, "shaders/gl33/ui_fs.glsl");
    if (shaders[1] == 0)
    {
        GetLogger()->Error("Cound not create fragment shader from file 'ui_fs.glsl'\n");
        return;
    }

    m_program = LinkProgram(2, shaders);
    if (m_program == 0)
    {
        GetLogger()->Error("Cound not link shader program for interface renderer\n");
        return;
    }

    glDeleteShader(shaders[0]);
    glDeleteShader(shaders[1]);

    glUseProgram(m_program);

    // Create uniform buffer
    glGenBuffers(1, &m_uniformBuffer);

    m_uniforms.projectionMatrix = glm::ortho(0.0f, +1.0f, 0.0f, +1.0f);
    m_uniforms.color = { 1.0f, 1.0f, 1.0f, 1.0f };

    m_uniformsDirty = true;

    UpdateUniforms();

    // Bind uniform block to uniform buffer binding
    GLuint blockIndex = glGetUniformBlockIndex(m_program, "Uniforms");

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniformBuffer);
    glUniformBlockBinding(m_program, blockIndex, 0);

    // Set texture unit to 8th
    auto texture = glGetUniformLocation(m_program, "uni_Texture");
    glUniform1i(texture, 8);

    // Generic buffer
    glGenBuffers(1, &m_bufferVBO);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_bufferVBO);
    glBufferData(GL_COPY_WRITE_BUFFER, m_bufferCapacity, nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &m_bufferVAO);
    glBindVertexArray(m_bufferVAO);

    // White texture
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

    glUseProgram(0);

    GetLogger()->Info("CGL33UIRenderer created successfully\n");
}

CGL33UIRenderer::~CGL33UIRenderer()
{
    glDeleteProgram(m_program);
    glDeleteTextures(1, &m_whiteTexture);

    glDeleteBuffers(1, &m_bufferVBO);
    glDeleteVertexArrays(1, &m_bufferVAO);
}

void CGL33UIRenderer::SetProjection(float left, float right, float bottom, float top)
{
    Flush();

    m_uniforms.projectionMatrix = glm::ortho(left, right, bottom, top);
    m_uniformsDirty = true;
}

void CGL33UIRenderer::SetTexture(const Texture& texture)
{
    if (m_currentTexture == texture.id) return;

    Flush();

    glActiveTexture(GL_TEXTURE8);

    m_currentTexture = texture.id;

    if (m_currentTexture == 0)
        glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    else
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
}

void CGL33UIRenderer::SetColor(const glm::vec4& color)
{
    Flush();

    m_uniforms.color = color;
    m_uniformsDirty = true;
}

void CGL33UIRenderer::DrawPrimitive(PrimitiveType type, int count, const Vertex2D* vertices)
{
    // If too much data would be buffered, flush
    size_t totalSize = (m_buffer.size() + count) * sizeof(Vertex2D);

    if (totalSize > m_bufferCapacity)
        Flush();

    // Buffer data
    GLint first = m_buffer.size();

    m_buffer.insert(m_buffer.end(), vertices, vertices + count);
    m_types.push_back(TranslateGfxPrimitive(type));
    m_firsts.push_back(first);
    m_counts.push_back(count);
}

void CGL33UIRenderer::Flush()
{
    if (m_types.empty()) return;

    UpdateUniforms();

    glUseProgram(m_program);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uniformBuffer);

    // Increase buffer size if necessary
    size_t size = m_buffer.size() * sizeof(Vertex2D);

    if (m_bufferCapacity < size)
        m_bufferCapacity = size;

    // Send new vertices to GPU
    glBindBuffer(GL_ARRAY_BUFFER, m_bufferVBO);
    glBufferData(GL_ARRAY_BUFFER, m_bufferCapacity, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, m_buffer.data());

    // Respecify vertex attributes
    glBindVertexArray(m_bufferVAO);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
        reinterpret_cast<void*>(offsetof(Vertex2D, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
        reinterpret_cast<void*>(offsetof(Vertex2D, uv)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2D),
        reinterpret_cast<void*>(offsetof(Vertex2D, color)));

    // Draw primitives by grouping by type
    for (size_t i = 0; i < m_types.size(); i++)
    {
        size_t count = 1;

        for (; i + count < m_types.size(); count++)
        {
            if (m_types[i] != m_types[i + count])
                break;
        }

        glMultiDrawArrays(m_types[i], &m_firsts[i], &m_counts[i], count);

        i += count;
    }

    // Clear buffers
    m_buffer.clear();
    m_types.clear();
    m_firsts.clear();
    m_counts.clear();

    m_device->Restore();
}

void CGL33UIRenderer::UpdateUniforms()
{
    if (!m_uniformsDirty) return;

    glBindBuffer(GL_COPY_WRITE_BUFFER, m_uniformBuffer);
    glBufferData(GL_COPY_WRITE_BUFFER, sizeof(Uniforms), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(Uniforms), &m_uniforms);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}


CGL33TerrainRenderer::CGL33TerrainRenderer(CGL33Device* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CGL33TerrainRenderer\n");

    GLint shaders[2] = {};

    shaders[0] = LoadShader(GL_VERTEX_SHADER, "shaders/gl33/terrain_vs.glsl");
    if (shaders[0] == 0)
    {
        GetLogger()->Error("Cound not create vertex shader from file 'terrain_vs.glsl'\n");
        return;
    }

    shaders[1] = LoadShader(GL_FRAGMENT_SHADER, "shaders/gl33/terrain_fs.glsl");
    if (shaders[1] == 0)
    {
        GetLogger()->Error("Cound not create fragment shader from file 'terrain_fs.glsl'\n");
        return;
    }

    m_program = LinkProgram(2, shaders);
    if (m_program == 0)
    {
        GetLogger()->Error("Cound not link shader program for terrain renderer\n");
        return;
    }

    glDeleteShader(shaders[0]);
    glDeleteShader(shaders[1]);

    glUseProgram(m_program);

    // Setup uniforms
    auto identity = glm::identity<glm::mat4>();

    uni_projectionMatrix = glGetUniformLocation(m_program, "uni_ProjectionMatrix");
    uni_viewMatrix = glGetUniformLocation(m_program, "uni_ViewMatrix");
    uni_cameraMatrix = glGetUniformLocation(m_program, "uni_CameraMatrix");
    uni_shadowMatrix = glGetUniformLocation(m_program, "uni_ShadowMatrix");
    uni_modelMatrix = glGetUniformLocation(m_program, "uni_ModelMatrix");
    uni_normalMatrix = glGetUniformLocation(m_program, "uni_NormalMatrix");
    uni_lightPosition = glGetUniformLocation(m_program, "uni_LightPosition");
    uni_lightIntensity = glGetUniformLocation(m_program, "uni_LightIntensity");
    uni_lightColor = glGetUniformLocation(m_program, "uni_LightColor");

    // Set texture units to 10th and 11th
    auto texture = glGetUniformLocation(m_program, "uni_PrimaryTexture");
    glUniform1i(texture, 10);

    texture = glGetUniformLocation(m_program, "uni_SecondaryTexture");
    glUniform1i(texture, 11);

    texture = glGetUniformLocation(m_program, "uni_ShadowMap");
    glUniform1i(texture, 12);

    // White texture
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

    glUseProgram(0);

    GetLogger()->Info("CGL33TerrainRenderer created successfully\n");
}

CGL33TerrainRenderer::~CGL33TerrainRenderer()
{
    glDeleteProgram(m_program);
    glDeleteTextures(1, &m_whiteTexture);
}

void CGL33TerrainRenderer::Begin()
{
    glUseProgram(m_program);
}

void CGL33TerrainRenderer::End()
{
    m_device->Restore();
}

void CGL33TerrainRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    glUniformMatrix4fv(uni_projectionMatrix, 1, GL_FALSE, value_ptr(matrix));
}

void CGL33TerrainRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    glm::mat4 scale(1.0f);
    scale[2][2] = -1.0f;

    auto viewMatrix = scale * matrix;
    auto cameraMatrix = glm::inverse(viewMatrix);

    glUniformMatrix4fv(uni_viewMatrix, 1, GL_FALSE, value_ptr(viewMatrix));
    glUniformMatrix4fv(uni_cameraMatrix, 1, GL_FALSE, value_ptr(cameraMatrix));
}

void CGL33TerrainRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    auto normalMatrix = glm::transpose(glm::inverse(glm::mat3(matrix)));

    glUniformMatrix4fv(uni_modelMatrix, 1, GL_FALSE, value_ptr(matrix));
    glUniformMatrix3fv(uni_normalMatrix, 1, GL_FALSE, value_ptr(normalMatrix));
}

void CGL33TerrainRenderer::SetShadowMatrix(const glm::mat4& matrix)
{
    glUniformMatrix4fv(uni_shadowMatrix, 1, GL_FALSE, value_ptr(matrix));
}

void CGL33TerrainRenderer::SetPrimaryTexture(const Texture& texture)
{
    if (m_primaryTexture == texture.id) return;

    m_primaryTexture = texture.id;

    glActiveTexture(GL_TEXTURE10);

    if (texture.id == 0)
        glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    else
        glBindTexture(GL_TEXTURE_2D, texture.id);
}

void CGL33TerrainRenderer::SetSecondaryTexture(const Texture& texture)
{
    if (m_secondaryTexture == texture.id) return;

    m_secondaryTexture = texture.id;

    glActiveTexture(GL_TEXTURE11);

    if (texture.id == 0)
        glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    else
        glBindTexture(GL_TEXTURE_2D, texture.id);
}

void CGL33TerrainRenderer::SetShadowMap(const Texture& texture)
{
    if (m_shadowMap == texture.id) return;

    m_shadowMap = texture.id;

    glActiveTexture(GL_TEXTURE12);

    if (texture.id == 0)
        glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    else
        glBindTexture(GL_TEXTURE_2D, texture.id);
}

void CGL33TerrainRenderer::SetLight(const glm::vec4& position, const float& intensity, const glm::vec3& color)
{
    glUniform4fv(uni_lightPosition, 1, glm::value_ptr(position));
    glUniform1f(uni_lightIntensity, intensity);
    glUniform3fv(uni_lightColor, 1, glm::value_ptr(color));
}

void CGL33TerrainRenderer::DrawObject(const glm::mat4& matrix, const CVertexBuffer* buffer)
{
    auto b = dynamic_cast<const CGL33VertexBuffer*>(buffer);

    if (b == nullptr)
    {
        GetLogger()->Error("No vertex buffer");
        return;
    }

    SetModelMatrix(matrix);
    glBindVertexArray(b->GetVAO());

    glDrawArrays(TranslateGfxPrimitive(b->GetType()), 0, b->Size());
}

void CGL33TerrainRenderer::Flush()
{

}

} // namespace Gfx
