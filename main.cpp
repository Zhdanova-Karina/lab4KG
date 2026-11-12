#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm/glm.hpp"
#include "glm/glm/gtc/matrix_transform.hpp"
#include "glm/glm/gtc/type_ptr.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;

glm::vec3 cameraPos = glm::vec3(0.0f, 1.6f, 2.5f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = -90.0f, pitch = 0.0f;
bool firstMouse = true;
float lastX = SCR_WIDTH / 2.0f, lastY = SCR_HEIGHT / 2.0f;
float deltaTime = 0.0f, lastFrame = 0.0f;

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn), ypos = static_cast<float>(yposIn);
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoffset = xpos - lastX, yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    float sensitivity = 0.1f;
    yaw += xoffset * sensitivity; pitch += yoffset * sensitivity;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void processInput(GLFWwindow* window) {
    float speed = 2.5f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 2.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += speed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= speed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * speed;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        cameraPos += speed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) cameraPos -= speed * cameraUp;

    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) {
        pitch = -89.0f; yaw = -90.0f;
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cameraFront = glm::normalize(front);
    }

    cameraPos.x = glm::clamp(cameraPos.x, -3.4f, 3.4f);
    cameraPos.z = glm::clamp(cameraPos.z, -3.4f, 3.4f);
    cameraPos.y = glm::clamp(cameraPos.y, 0.3f, 3.7f);
}

const char* depthVS = R"(#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() { gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0); })";

const char* depthFS = R"(#version 330 core
void main() { })";

const char* mainVS = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
uniform mat4 model; uniform mat4 view; uniform mat4 projection; uniform mat4 lightSpaceMatrix;
out vec3 FragPos; out vec3 Normal; out vec4 FragPosLightSpace; out vec2 TexCoord;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
})";

const char* mainFS = R"(#version 330 core
in vec3 FragPos; in vec3 Normal; in vec4 FragPosLightSpace; in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D shadowMap;
uniform sampler2D diffuseTexture;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform vec3 lightColor;
uniform vec3 lightPos;            // потолочная лампа — общий свет, без теней
uniform float Ka;
uniform float Kd;
uniform float Ks;
uniform float p;
uniform bool isLamp;
uniform bool useTexture;

// Две лампы-бра на стене с дверью — именно от них считаются тени
uniform vec3 wallLampPos[2];
uniform vec3 wallLampColor;

// Точка, от которой рендерится shadow map (между бра)
uniform vec3 shadowLightPos;

float ShadowCalculation(vec4 fragPosLightSpace, float bias) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    return currentDepth - bias > closestDepth ? 1.0 : 0.0;
}

void main() {
    if (isLamp) {
        FragColor = vec4(objectColor * 2.0f, 1.0f);
        return;
    }

    vec3 baseColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : objectColor;

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);

    // ===== Потолочная лампа: общий свет, БЕЗ теней =====
    vec3 ambient = Ka * lightColor;
    vec3 L = normalize(lightPos - FragPos);
    float diff = max(dot(N, L), 0.0);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), p);
    float distance = length(lightPos - FragPos);
    float attenuation = 1.0 / (1.0 + 0.05 * distance + 0.01 * (distance * distance));
    vec3 lighting = (Kd * diff + Ks * spec) * lightColor * attenuation;

    // ===== Бра: свет + тень =====
    vec3 Lsh = normalize(shadowLightPos - FragPos);
    float bias = max(0.0008 * (1.0 - dot(N, Lsh)), 0.0002);
    float shadow = ShadowCalculation(FragPosLightSpace, bias);

    for (int i = 0; i < 2; i++) {
        vec3 Lw = normalize(wallLampPos[i] - FragPos);
        float diffW = max(dot(N, Lw), 0.0);
        vec3 Rw = reflect(-Lw, N);
        float specW = pow(max(dot(Rw, V), 0.0), p);
        float distW = length(wallLampPos[i] - FragPos);
        float attW = 1.0 / (1.0 + 0.10 * distW + 0.03 * (distW * distW));
        lighting += (Kd * diffW + Ks * specW) * wallLampColor * attW * (1.0 - shadow);
    }

    vec3 finalColor = (ambient + lighting) * baseColor;
    FragColor = vec4(finalColor, 1.0);
})";

unsigned int compileShader(GLenum type, const char* source) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    int success;
    glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[1024];
        glGetShaderInfoLog(s, 1024, nullptr, info);
        std::cout << "SHADER COMPILE ERROR (type " << type << "):\n" << info << std::endl;
    }
    return s;
}

unsigned int createProgram(const char* vs, const char* fs) {
    unsigned int p = glCreateProgram();
    glAttachShader(p, compileShader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compileShader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    int success;
    glGetProgramiv(p, GL_LINK_STATUS, &success);
    if (!success) {
        char info[1024];
        glGetProgramInfoLog(p, 1024, nullptr, info);
        std::cout << "PROGRAM LINK ERROR:\n" << info << std::endl;
    }
    return p;
}

unsigned int loadTexture(const char* path) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Texture loaded: " << path << std::endl;
    }
    else {
        std::cout << "Failed to load texture: " << path << std::endl;
    }
    stbi_image_free(data);
    return tex;
}

struct Vertex { glm::vec3 pos; glm::vec3 normal; glm::vec2 texCoord; };
std::vector<Vertex> cubeVerts;
std::vector<unsigned int> cubeIndices;

std::vector<Vertex> cylinderVerts, sphereVerts;
std::vector<unsigned int> cylinderIndices, sphereIndices;
unsigned int cylVAO, cylVBO, cylEBO;
unsigned int sphVAO, sphVBO, sphEBO;

void buildCube() {
    glm::vec3 n[6] = { {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0} };
    glm::vec2 uv[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
    for (int f = 0; f < 6; f++) {
        glm::vec3 u = (fabs(n[f].z) > 0.5f) ? glm::vec3(1, 0, 0) : (fabs(n[f].x) > 0.5f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0));
        glm::vec3 v = (fabs(n[f].y) > 0.5f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::vec3 b = n[f] * 0.5f;
        glm::vec3 c[4] = { b - u * 0.5f - v * 0.5f, b + u * 0.5f - v * 0.5f, b + u * 0.5f + v * 0.5f, b - u * 0.5f + v * 0.5f };
        for (int i = 0; i < 4; i++) cubeVerts.push_back({ c[i], n[f], uv[i] });
        unsigned int bi = f * 4;
        cubeIndices.insert(cubeIndices.end(), { bi, bi + 1, bi + 2, bi, bi + 2, bi + 3 });
    }
}

void buildCylinder() {
    int segments = 32;
    float radius = 0.5f;
    float halfHeight = 0.5f;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        glm::vec3 n = glm::normalize(glm::vec3(x, 0.0f, z));
        glm::vec2 uv = glm::vec2((float)i / segments, 0.0f);
        cylinderVerts.push_back({ glm::vec3(x, halfHeight, z), n, uv });
        cylinderVerts.push_back({ glm::vec3(x, -halfHeight, z), n, glm::vec2(uv.x, 1.0f) });
    }
    for (int i = 0; i < segments; ++i) {
        int base = i * 2;
        cylinderIndices.push_back(base);
        cylinderIndices.push_back(base + 1);
        cylinderIndices.push_back(base + 2);
        cylinderIndices.push_back(base + 2);
        cylinderIndices.push_back(base + 1);
        cylinderIndices.push_back(base + 3);
    }
}

void buildSphere() {
    int sectors = 32;
    int stacks = 16;
    float radius = 0.5f;
    for (int i = 0; i <= stacks; ++i) {
        float V = (float)i / stacks;
        float phi = V * glm::pi<float>();
        for (int j = 0; j <= sectors; ++j) {
            float U = (float)j / sectors;
            float theta = U * (glm::pi<float>() * 2.0f);
            float x = cos(theta) * sin(phi);
            float y = cos(phi);
            float z = sin(theta) * sin(phi);
            glm::vec3 pos = glm::vec3(x, y, z) * radius;
            sphereVerts.push_back({ pos, glm::normalize(pos), glm::vec2(U, V) });
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            int first = (i * (sectors + 1)) + j;
            int second = first + sectors + 1;
            sphereIndices.push_back(first);
            sphereIndices.push_back(second);
            sphereIndices.push_back(first + 1);
            sphereIndices.push_back(second);
            sphereIndices.push_back(second + 1);
            sphereIndices.push_back(first + 1);
        }
    }
}

void setupMesh(unsigned int& VAO, unsigned int& VBO, unsigned int& EBO,
    const std::vector<Vertex>& verts, const std::vector<unsigned int>& indices) {
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord)); glEnableVertexAttribArray(2);
}

enum ObjectType { FLOOR, WALL, FRAME, PAINTING, LAMP, SCULPTURE, CEILING, GOLD_BAR, RED_RIBBON };

struct Material { glm::vec3 color; float Ka; float Kd; float Ks; float p; bool isEmissive; bool useTexture; };

struct Object {
    glm::vec3 pos;
    glm::vec3 scale;
    Material mat;
    int textureIndex;
    ObjectType type;
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 pivot = glm::vec3(0.0f);
};

glm::mat4 buildModelMatrix(const Object& obj) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.pos);
    glm::vec3 p = obj.pivot * obj.scale;
    model = glm::translate(model, p);
    model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
    model = glm::translate(model, -p);
    model = glm::scale(model, obj.scale);
    return model;
}

// ============ ЗАГРУЗЧИК МОДЕЛЕЙ ============

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    unsigned int VAO = 0, VBO = 0, EBO = 0;

    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    void Draw() {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

class Model {
public:
    std::vector<MeshData> meshes;

    Model(const std::string& path) { loadModel(path); }
    void Draw() { for (auto& m : meshes) m.Draw(); }

private:
    void loadModel(const std::string& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << " (path: " << path << ")" << std::endl;
            return;
        }
        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode* node, const aiScene* scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    MeshData processMesh(aiMesh* mesh, const aiScene* scene) {
        MeshData md;
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex v;
            v.pos = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            v.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            if (mesh->mTextureCoords[0]) {
                v.texCoord = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            }
            else {
                v.texCoord = glm::vec2(0.0f);
            }
            md.vertices.push_back(v);
        }
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                md.indices.push_back(face.mIndices[j]);
            }
        }
        md.setupMesh();
        return md;
    }
};
// ===========================================

void addGoldBarsAndRibbons(std::vector<Object>& objects, glm::vec3 center, float yawDeg,
    const Material& matGold, const Material& matRibbon) {
    float offset = 0.8f;
    float barY = 0.5f;
    float barHeight = 1.0f;
    float barThickness = 0.05f;
    float ribbonY = 0.8f;
    float ribbonThickness = 0.02f;
    float ribbonWidth = offset * 2.0f;

    glm::vec3 corners[4] = {
        {-offset, 0.0f, -offset},
        { offset, 0.0f, -offset},
        {-offset, 0.0f,  offset},
        { offset, 0.0f,  offset}
    };

    float rad = glm::radians(yawDeg);
    for (int i = 0; i < 4; i++) {
        glm::vec3 rotated = glm::vec3(
            corners[i].x * cos(rad) - corners[i].z * sin(rad),
            0.0f,
            corners[i].x * sin(rad) + corners[i].z * cos(rad)
        );
        glm::vec3 finalPos = center + rotated;
        finalPos.y = barY;
        objects.push_back({ finalPos, {barThickness, barHeight, barThickness}, matGold, 0, GOLD_BAR });
    }

    glm::vec3 ribbonDirs[4] = {
        {0.0f, 0.0f, -offset},
        {0.0f, 0.0f,  offset},
        {-offset, 0.0f, 0.0f},
        { offset, 0.0f, 0.0f}
    };
    glm::vec3 ribbonScales[4] = {
        {ribbonWidth, ribbonThickness, 0.04f},
        {ribbonWidth, ribbonThickness, 0.04f},
        {0.04f, ribbonThickness, ribbonWidth},
        {0.04f, ribbonThickness, ribbonWidth}
    };

    for (int i = 0; i < 4; i++) {
        glm::vec3 rotatedDir = glm::vec3(
            ribbonDirs[i].x * cos(rad) - ribbonDirs[i].z * sin(rad),
            0.0f,
            ribbonDirs[i].x * sin(rad) + ribbonDirs[i].z * cos(rad)
        );
        glm::vec3 finalPos = center + rotatedDir;
        finalPos.y = ribbonY;
        objects.push_back({ finalPos, ribbonScales[i], matRibbon, 0, RED_RIBBON, glm::vec3(0.0f, yawDeg, 0.0f) });
    }
}

struct StatueTransform {
    Model* model;
    glm::vec3 position;
    float yawDeg;
    float scale;
    unsigned int textureID;
};

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Museum (Wall Lamps Shadows)", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glEnable(GL_DEPTH_TEST);

    buildCube();
    buildCylinder();
    buildSphere();

    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, cubeVerts.size() * sizeof(Vertex), cubeVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cubeIndices.size() * sizeof(unsigned int), cubeIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord)); glEnableVertexAttribArray(2);

    setupMesh(cylVAO, cylVBO, cylEBO, cylinderVerts, cylinderIndices);
    setupMesh(sphVAO, sphVBO, sphEBO, sphereVerts, sphereIndices);

    unsigned int depthProgram = createProgram(depthVS, depthFS);
    unsigned int mainProgram = createProgram(mainVS, mainFS);

    unsigned int woodTexture = loadTexture("wood.jpg");
    unsigned int floorTexture = loadTexture("floor.jpg");
    unsigned int marbleTexture = loadTexture("marble.jpg");
    unsigned int goldTexture = loadTexture("gold.jpg");
    unsigned int ribbonTexture = loadTexture("ribbon.jpg");

    unsigned int catTex = loadTexture("models/cat_diffuse.jpg");
    unsigned int horseTex = loadTexture("models/horse_diffuse.jpg");
    unsigned int lionTex = loadTexture("models/lion_diffuse.jpg");

    unsigned int paintTex[6];
    for (int i = 0; i < 6; i++) {
        std::string path = "paint" + std::to_string(i + 1) + ".jpg";
        paintTex[i] = loadTexture(path.c_str());
    }

    Model* catModel = new Model("models/concrete_cat_statue_4k.gltf");
    Model* horseModel = new Model("models/horse_head_4k.gltf");
    Model* lionModel = new Model("models/lion_head_4k.gltf");

    std::vector<StatueTransform> statues = {
        { lionModel,  glm::vec3(0.0f, 0.30f, -2.6f),   0.0f, 3.5f, lionTex },
        { horseModel, glm::vec3(-2.9f, 0.30f,  0.0f),  90.0f, 3.5f, horseTex },
        { catModel,   glm::vec3(2.9f, 0.30f,  0.0f), -90.0f, 5.0f, catTex }
    };

    unsigned int depthMapFBO, depthMap;
    glGenFramebuffers(1, &depthMapFBO); glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Потолочная лампа — общий свет, БЕЗ теней
    glm::vec3 lightPos = glm::vec3(0.0f, 3.8f, 0.0f);
    glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

    // Две лампы-бра на стене с дверью — от них считаются тени
    glm::vec3 wallLampPositions[2] = {
        glm::vec3(-1.5f, 2.2f, 3.85f),
        glm::vec3(1.5f, 2.2f, 3.85f)
    };
    glm::vec3 wallLampColor = glm::vec3(1.0f, 0.9f, 0.7f);

    // Точка, от которой рендерится shadow map (между бра, чуть впереди стены)
    glm::vec3 shadowLightPos = glm::vec3(0.0f, 2.2f, 3.3f);
    glm::vec3 shadowLightTarget = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 lightProj = glm::ortho(-4.5f, 4.5f, -4.5f, 4.5f, 0.1f, 12.0f);
    glm::mat4 lightView = glm::lookAt(shadowLightPos, shadowLightTarget, glm::vec3(0, 1, 0));
    glm::mat4 lightSpaceMatrix = lightProj * lightView;

    Material matFloor = { {0.55f, 0.27f, 0.07f}, 0.2f, 0.8f, 0.0f,  1.0f,  false, true };
    Material matWall = { {0.9f, 0.9f, 0.9f},    0.2f, 0.8f, 0.0f,  1.0f,  false, false };
    Material matCeiling = { {0.5f, 0.5f, 0.5f},    0.2f, 0.6f, 0.1f,  8.0f,  false, false };
    Material matWood = { {1.0f, 1.0f, 1.0f},    0.2f, 0.9f, 0.0f,  1.0f,  false, true };
    Material matPaint = { {1.0f, 1.0f, 1.0f},    0.2f, 0.8f, 0.0f,  1.0f,  false, true };
    Material matLamp = { {1.0f, 1.0f, 1.0f},    1.0f, 0.0f, 0.0f,  1.0f,  true,  false };
    Material matMarble = { {1.0f, 1.0f, 1.0f},    0.3f, 0.6f, 0.5f,  64.0f, false, true };
    Material matPedestal = { {0.85f, 0.85f, 0.85f}, 0.25f,0.7f, 0.2f,  32.0f, false, false };
    Material matGold = { {1.0f, 0.84f, 0.0f},   0.4f, 0.8f, 1.0f,  128.0f,false, true };
    Material matRibbon = { {0.8f, 0.0f, 0.0f},    0.5f, 0.5f, 0.2f,  16.0f, false, true };

    std::vector<Object> objects = {
        {{0, -0.05f, 0}, {8.0f, 0.1f, 8.0f}, matFloor, 0, FLOOR},
        {{0, 4.0f, 0}, {8.0f, 0.1f, 8.0f}, matCeiling, 0, CEILING},
        {{0, 2.0f, -4.0f}, {8.0f, 4.0f, 0.1f}, matWall, 0, WALL},
        {{-4.0f, 2.0f, 0}, {0.1f, 4.0f, 8.0f}, matWall, 0, WALL},
        {{4.0f, 2.0f, 0}, {0.1f, 4.0f, 8.0f}, matWall, 0, WALL},
        {{0, 2.0f, 4.0f}, {8.0f, 4.0f, 0.1f}, matWall, 0, WALL},

        // Дверь (передняя стена)
        {{0.0f, 1.25f, 3.92f}, {1.2f, 2.5f, 0.08f}, matWood, 0, WALL},
        {{-0.65f, 1.25f, 3.93f}, {0.1f, 2.6f, 0.1f}, matWood, 0, WALL},
        {{ 0.65f, 1.25f, 3.93f}, {0.1f, 2.6f, 0.1f}, matWood, 0, WALL},
        {{ 0.0f, 2.55f, 3.93f}, {1.4f, 0.1f, 0.1f}, matWood, 0, WALL},

        // ===== Две лампы-бра на стене с дверью =====
        {{wallLampPositions[0].x, wallLampPositions[0].y, wallLampPositions[0].z},
         {0.35f, 0.5f, 0.15f}, matLamp, 0, LAMP},
        {{wallLampPositions[1].x, wallLampPositions[1].y, wallLampPositions[1].z},
         {0.35f, 0.5f, 0.15f}, matLamp, 0, LAMP},

         // Золотые кронштейны под брами
         {{wallLampPositions[0].x, wallLampPositions[0].y - 0.30f, 3.90f},
          {0.08f, 0.12f, 0.08f}, matGold, 0, GOLD_BAR},
         {{wallLampPositions[1].x, wallLampPositions[1].y - 0.30f, 3.90f},
          {0.08f, 0.12f, 0.08f}, matGold, 0, GOLD_BAR},

          // Картины
          {{-1.8f, 2.2f, -3.94f}, {1.4f, 1.4f, 0.05f}, matWood, 0, FRAME},
          {{-1.8f, 2.2f, -3.92f}, {1.2f, 1.2f, 0.05f}, matPaint, 1, PAINTING},
          {{1.8f, 2.2f, -3.94f}, {1.4f, 1.4f, 0.05f}, matWood, 0, FRAME},
          {{1.8f, 2.2f, -3.92f}, {1.2f, 1.2f, 0.05f}, matPaint, 2, PAINTING},
          {{-3.94f, 2.2f, -1.5f}, {0.05f, 1.4f, 1.4f}, matWood, 0, FRAME},
          {{-3.92f, 2.2f, -1.5f}, {0.05f, 1.2f, 1.2f}, matPaint, 3, PAINTING},
          {{-3.94f, 2.2f, 1.5f}, {0.05f, 1.4f, 1.4f}, matWood, 0, FRAME},
          {{-3.92f, 2.2f, 1.5f}, {0.05f, 1.2f, 1.2f}, matPaint, 4, PAINTING},
          {{3.94f, 2.2f, -1.5f}, {0.05f, 1.4f, 1.4f}, matWood, 0, FRAME},
          {{3.92f, 2.2f, -1.5f}, {0.05f, 1.2f, 1.2f}, matPaint, 5, PAINTING},
          {{3.94f, 2.2f, 1.5f}, {0.05f, 1.4f, 1.4f}, matWood, 0, FRAME},
          {{3.92f, 2.2f, 1.5f}, {0.05f, 1.2f, 1.2f}, matPaint, 6, PAINTING},

          {lightPos, {0.3f, 0.1f, 0.3f}, matLamp, 0, LAMP},

          // Пьедесталы
          {{0.0f, 0.15f, -2.6f}, {1.2f, 0.30f, 1.2f}, matPedestal, 0, SCULPTURE},
          {{-2.9f, 0.15f, 0.0f}, {1.2f, 0.30f, 1.2f}, matPedestal, 0, SCULPTURE},
          {{ 2.9f, 0.15f, 0.0f}, {1.2f, 0.30f, 1.2f}, matPedestal, 0, SCULPTURE}
    };

    addGoldBarsAndRibbons(objects, glm::vec3(0.0f, 0.0f, -2.6f), 0.0f, matGold, matRibbon);
    addGoldBarsAndRibbons(objects, glm::vec3(-2.9f, 0.0f, 0.0f), 90.0f, matGold, matRibbon);
    addGoldBarsAndRibbons(objects, glm::vec3(2.9f, 0.0f, 0.0f), -90.0f, matGold, matRibbon);

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        processInput(window);

        // 1. Теневой проход — от бра-ламп
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(depthProgram);
        glUniformMatrix4fv(glGetUniformLocation(depthProgram, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        for (auto& obj : objects) {
            if (obj.mat.isEmissive) continue;
            glm::mat4 model = buildModelMatrix(obj);
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, cubeIndices.size(), GL_UNSIGNED_INT, 0);
        }

        for (const auto& st : statues) {
            if (!st.model || st.model->meshes.empty()) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), st.position);
            model = glm::rotate(model, glm::radians(st.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(st.scale));
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            st.model->Draw();
        }

        // Тень от дверной ручки
        {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.86f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(0.12f, 0.02f, 0.12f));
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "model"), 1, GL_FALSE, glm::value_ptr(m));
            glBindVertexArray(cylVAO);
            glDrawElements(GL_TRIANGLES, cylinderIndices.size(), GL_UNSIGNED_INT, 0);

            m = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.82f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(0.04f, 0.06f, 0.04f));
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "model"), 1, GL_FALSE, glm::value_ptr(m));
            glDrawElements(GL_TRIANGLES, cylinderIndices.size(), GL_UNSIGNED_INT, 0);

            m = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.77f));
            m = glm::scale(m, glm::vec3(0.16f));
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "model"), 1, GL_FALSE, glm::value_ptr(m));
            glBindVertexArray(sphVAO);
            glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. Основной проход
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(mainProgram);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)SCR_WIDTH / SCR_HEIGHT, 0.1f, 50.0f);

        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform3fv(glGetUniformLocation(mainProgram, "viewPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(glGetUniformLocation(mainProgram, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(mainProgram, "lightColor"), 1, glm::value_ptr(lightColor));
        glUniform3fv(glGetUniformLocation(mainProgram, "shadowLightPos"), 1, glm::value_ptr(shadowLightPos));

        glUniform3fv(glGetUniformLocation(mainProgram, "wallLampPos"), 2,
            glm::value_ptr(wallLampPositions[0]));
        glUniform3fv(glGetUniformLocation(mainProgram, "wallLampColor"), 1,
            glm::value_ptr(wallLampColor));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthMap);
        glUniform1i(glGetUniformLocation(mainProgram, "shadowMap"), 0);

        for (auto& obj : objects) {
            glm::mat4 model = buildModelMatrix(obj);
            glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

            glUniform3fv(glGetUniformLocation(mainProgram, "objectColor"), 1, glm::value_ptr(obj.mat.color));
            glUniform1f(glGetUniformLocation(mainProgram, "Ka"), obj.mat.Ka);
            glUniform1f(glGetUniformLocation(mainProgram, "Kd"), obj.mat.Kd);
            glUniform1f(glGetUniformLocation(mainProgram, "Ks"), obj.mat.Ks);
            glUniform1f(glGetUniformLocation(mainProgram, "p"), obj.mat.p);
            glUniform1i(glGetUniformLocation(mainProgram, "isLamp"), obj.mat.isEmissive ? 1 : 0);
            glUniform1i(glGetUniformLocation(mainProgram, "useTexture"), obj.mat.useTexture ? 1 : 0);

            glActiveTexture(GL_TEXTURE1);
            unsigned int chosenTex = floorTexture;
            if (obj.type == FLOOR) chosenTex = floorTexture;
            else if (obj.type == FRAME) chosenTex = woodTexture;
            else if (obj.type == PAINTING) chosenTex = paintTex[obj.textureIndex - 1];
            else if (obj.type == SCULPTURE) chosenTex = marbleTexture;
            else if (obj.type == GOLD_BAR) chosenTex = goldTexture;
            else if (obj.type == RED_RIBBON) chosenTex = ribbonTexture;

            glBindTexture(GL_TEXTURE_2D, chosenTex);
            glUniform1i(glGetUniformLocation(mainProgram, "diffuseTexture"), 1);

            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, cubeIndices.size(), GL_UNSIGNED_INT, 0);
        }

        // ===== ДВЕРНАЯ РУЧКА =====
        glUniform3fv(glGetUniformLocation(mainProgram, "objectColor"), 1, glm::value_ptr(glm::vec3(1.0f)));
        glUniform1f(glGetUniformLocation(mainProgram, "Ka"), 0.4f);
        glUniform1f(glGetUniformLocation(mainProgram, "Kd"), 0.8f);
        glUniform1f(glGetUniformLocation(mainProgram, "Ks"), 1.0f);
        glUniform1f(glGetUniformLocation(mainProgram, "p"), 128.0f);
        glUniform1i(glGetUniformLocation(mainProgram, "isLamp"), 0);
        glUniform1i(glGetUniformLocation(mainProgram, "useTexture"), 1);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, goldTexture);
        glUniform1i(glGetUniformLocation(mainProgram, "diffuseTexture"), 1);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.86f));
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(0.12f, 0.02f, 0.12f));
        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(cylVAO);
        glDrawElements(GL_TRIANGLES, cylinderIndices.size(), GL_UNSIGNED_INT, 0);

        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.82f));
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(0.04f, 0.06f, 0.04f));
        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glDrawElements(GL_TRIANGLES, cylinderIndices.size(), GL_UNSIGNED_INT, 0);

        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 1.2f, 3.77f));
        model = glm::scale(model, glm::vec3(0.16f));
        glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(sphVAO);
        glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);

        // ===== СВЕТЯЩИЕСЯ ПЛАФОНЫ БРА =====
        glUniform3fv(glGetUniformLocation(mainProgram, "objectColor"), 1, glm::value_ptr(glm::vec3(1.0f)));
        glUniform1f(glGetUniformLocation(mainProgram, "Ka"), 1.0f);
        glUniform1f(glGetUniformLocation(mainProgram, "Kd"), 0.0f);
        glUniform1f(glGetUniformLocation(mainProgram, "Ks"), 0.0f);
        glUniform1f(glGetUniformLocation(mainProgram, "p"), 1.0f);
        glUniform1i(glGetUniformLocation(mainProgram, "isLamp"), 1);
        glUniform1i(glGetUniformLocation(mainProgram, "useTexture"), 0);

        glBindVertexArray(sphVAO);
        for (int i = 0; i < 2; i++) {
            glm::mat4 lampModel = glm::translate(glm::mat4(1.0f),
                wallLampPositions[i] + glm::vec3(0.0f, 0.05f, -0.12f));
            lampModel = glm::scale(lampModel, glm::vec3(0.28f));
            glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE,
                glm::value_ptr(lampModel));
            glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }
        glUniform1i(glGetUniformLocation(mainProgram, "isLamp"), 0);

        // Отрисовка статуй
        glUniform3fv(glGetUniformLocation(mainProgram, "objectColor"), 1, glm::value_ptr(glm::vec3(1.0f)));
        glUniform1f(glGetUniformLocation(mainProgram, "Ka"), 0.3f);
        glUniform1f(glGetUniformLocation(mainProgram, "Kd"), 0.7f);
        glUniform1f(glGetUniformLocation(mainProgram, "Ks"), 0.1f);
        glUniform1f(glGetUniformLocation(mainProgram, "p"), 16.0f);
        glUniform1i(glGetUniformLocation(mainProgram, "isLamp"), 0);
        glUniform1i(glGetUniformLocation(mainProgram, "useTexture"), 1);

        for (const auto& st : statues) {
            if (!st.model || st.model->meshes.empty()) continue;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), st.position);
            m = glm::rotate(m, glm::radians(st.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(st.scale));
            glUniformMatrix4fv(glGetUniformLocation(mainProgram, "model"), 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, st.textureID);
            glUniform1i(glGetUniformLocation(mainProgram, "diffuseTexture"), 1);

            st.model->Draw();
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}
