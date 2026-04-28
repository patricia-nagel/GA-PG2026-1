/*
 * Trabalho Grau A - Leitor e Visualizador de Cenas 3D com OpenGL Moderna
 * Disciplina: Processamento Gráfico / Computação Gráfica - Unisinos
 *
 * Funcionalidades implementadas:
 *  - Leitura de arquivos .obj (posição, normal, texCoord) -> VAO/VBO (8 floats/vértice)
 *  - 3 objetos na cena simultâneos, seleção via TAB
 *  - Transformações por objeto: rotação (X/Y/Z), translação (setas + PgUp/PgDn), escala (R/F)
 *  - Câmera FPS com teclado (WASD/QE) e mouse
 *  - Alternância projeção Perspectiva / Ortográfica (tecla O)
 *  - Modelo de iluminação de Phong (Ambiente + Difusa + Especular) nos shaders
 *  - Ajuste em tempo real de ka, kd, ks e shininess via teclado (modo M)
 *  - Modo wireframe sobreposto (tecla B)
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

using namespace std;

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Camera.h"
#include "Mesh.h"
#include "LoadOBJ.h"

// ===========================================================================
// Shaders
// ===========================================================================

const GLchar* vertexShaderSource = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos   = vec3(worldPos);
    Normal    = normalMatrix * aNormal;
    TexCoord  = aTexCoord;
    gl_Position = projection * view * worldPos;
}
)glsl";

// Fragment Shader com iluminacao de Phong completa
const GLchar* fragmentShaderSource = R"glsl(
#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec3 viewPos;

// Luz pontual
uniform vec3 lightPos;
uniform vec3 lightColor;

// Material (ajustavel em tempo real)
uniform vec3  ka;
uniform vec3  kd;
uniform vec3  ks;
uniform float shininess;
uniform vec3  objectColor;

void main()
{
    vec3 norm     = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir  = normalize(viewPos - FragPos);
    vec3 reflDir  = reflect(-lightDir, norm);

    // Componente Ambiente
    vec3 ambient  = ka * lightColor;

    // Componente Difusa
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 diffuse  = kd * diff * lightColor;

    // Componente Especular
    float spec    = pow(max(dot(viewDir, reflDir), 0.0), shininess);
    vec3 specular = ks * spec * lightColor;

    vec3 result   = (ambient + diffuse + specular) * objectColor;
    FragColor     = vec4(result, 1.0);
}
)glsl";

// ===========================================================================
// Globais
// ===========================================================================

const GLuint WIDTH = 800, HEIGHT = 600;

Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
float deltaTime  = 0.0f;
float lastFrame  = 0.0f;
float lastX      = WIDTH  / 2.0f;
float lastY      = HEIGHT / 2.0f;
bool  firstMouse = true;

int  selectedObject = 0;
bool wireframe      = false;
bool usePerspective = true;

// Modo de edicao de material
// componente: 0=ka  1=kd  2=ks  3=shininess
int  editComponent = 1;   // começa em kd
bool materialMode  = false;

vector<Mesh> meshes;

// ===========================================================================
// Prototipos
// ===========================================================================
GLuint setupShaders();
void   printMaterial(int idx);
void   key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void   mouse_callback(GLFWwindow* window, double xpos, double ypos);
void   scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// ===========================================================================
// MAIN
// ===========================================================================
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "Visualizador 3D | TAB=Obj | WASD=Cam | M=Material | B=Wire | O=Proj", nullptr, nullptr);
    if (!window) { cerr << "Falha ao criar janela GLFW\n"; glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    { cerr << "Falha ao inicializar GLAD\n"; return -1; }

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << "\n\n";

    // ---- Configura e carrega 3 objetos ----
    // EDITE os caminhos abaixo para os seus .obj
    struct ObjCfg {
        string    path;
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec3 ka, kd, ks;
        float     shininess;
    };
    vector<ObjCfg> configs = {
        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3(-2.5f, 0.0f, 0.0f),
          glm::vec3(0.85f, 0.20f, 0.20f),
          glm::vec3(0.10f), glm::vec3(0.80f), glm::vec3(0.50f), 32.0f },

        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3( 0.0f, 0.0f, 0.0f),
          glm::vec3(0.20f, 0.80f, 0.25f),
          glm::vec3(0.10f), glm::vec3(0.80f), glm::vec3(0.30f), 16.0f },

        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3( 2.5f, 0.0f, 0.0f),
          glm::vec3(0.20f, 0.40f, 0.90f),
          glm::vec3(0.20f), glm::vec3(0.70f), glm::vec3(0.90f), 128.0f },
    };

    for (auto& cfg : configs)
    {
        Mesh m;
        loadSimpleOBJ(cfg.path, m);
        m.position  = cfg.pos;
        m.color     = cfg.color;
        m.ka        = cfg.ka;
        m.kd        = cfg.kd;
        m.ks        = cfg.ks;
        m.shininess = cfg.shininess;
        meshes.push_back(m);
    }

    cout << "\n=== " << meshes.size() << " objetos carregados ===\n";
    cout << "TAB=selecionar | M=material | B=wireframe | O=projecao\n";
    cout << "No modo M: 1=ka  2=kd  3=ks  4=shininess  +/-=ajustar\n\n";
    printMaterial(selectedObject);

    // ---- Shaders ----
    GLuint shaderID = setupShaders();
    glUseProgram(shaderID);
    glEnable(GL_DEPTH_TEST);

    glm::vec3 lightPos   = glm::vec3(3.0f, 5.0f, 3.0f);
    glm::vec3 lightColor = glm::vec3(1.0f);
    glUniform3fv(glGetUniformLocation(shaderID, "lightPos"),   1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(shaderID, "lightColor"), 1, glm::value_ptr(lightColor));

    // ===========================================================================
    // Game loop
    // ===========================================================================
    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;

        glfwPollEvents();

        // ---- Movimento de camera ----
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.processKeyboard("FORWARD",  deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.processKeyboard("BACKWARD", deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.processKeyboard("LEFT",     deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.processKeyboard("RIGHT",    deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.processKeyboard("UP",       deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.processKeyboard("DOWN",     deltaTime);

        // ---- Ajuste continuo de material (so quando modo M ativo) ----
        if (materialMode && !meshes.empty())
        {
            Mesh& sel  = meshes[selectedObject];
            float step = 0.5f * deltaTime;  // suave, dependente do framerate

            bool up   = (glfwGetKey(window, GLFW_KEY_KP_ADD)      == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_EQUAL)        == GLFW_PRESS);
            bool down = (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_MINUS)        == GLFW_PRESS);

            if (up || down)
            {
                float dir = up ? 1.0f : -1.0f;
                switch (editComponent)
                {
                    case 0: sel.ka = glm::clamp(sel.ka + dir * step, 0.0f, 1.0f); break;
                    case 1: sel.kd = glm::clamp(sel.kd + dir * step, 0.0f, 1.0f); break;
                    case 2: sel.ks = glm::clamp(sel.ks + dir * step, 0.0f, 1.0f); break;
                    case 3: sel.shininess = glm::clamp(sel.shininess + dir * 25.0f * deltaTime, 1.0f, 256.0f); break;
                }

                // Throttle do print para ~4x por segundo
                static float lastPrint = 0.0f;
                if (now - lastPrint > 0.25f) { printMaterial(selectedObject); lastPrint = now; }
            }
        }

        // ---- Translacao continua do objeto selecionado ----
        if (!meshes.empty())
        {
            Mesh& sel = meshes[selectedObject];
            float ts  = 2.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_LEFT)      == GLFW_PRESS) sel.position.x -= ts;
            if (glfwGetKey(window, GLFW_KEY_RIGHT)     == GLFW_PRESS) sel.position.x += ts;
            if (glfwGetKey(window, GLFW_KEY_UP)        == GLFW_PRESS) sel.position.y += ts;
            if (glfwGetKey(window, GLFW_KEY_DOWN)      == GLFW_PRESS) sel.position.y -= ts;
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) sel.position.z += ts;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) sel.position.z -= ts;
        }

        // ---- Clear ----
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- View / Projection ----
        glm::mat4 view = camera.getViewMatrix();
        glUniformMatrix4fv(glGetUniformLocation(shaderID, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniform3fv(glGetUniformLocation(shaderID, "viewPos"), 1, glm::value_ptr(camera.position));

        glm::mat4 proj = usePerspective
            ? glm::perspective(glm::radians(45.0f), (float)WIDTH / HEIGHT, 0.1f, 100.0f)
            : glm::ortho(-5.0f, 5.0f, -5.0f * (float)HEIGHT / WIDTH, 5.0f * (float)HEIGHT / WIDTH, 0.1f, 100.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderID, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

        // ---- Desenha objetos ----
        for (int i = 0; i < (int)meshes.size(); i++)
        {
            if (meshes[i].VAO == 0) continue;

            glm::mat4 model    = meshes[i].getModelMatrix();
            glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));

            glUniformMatrix4fv(glGetUniformLocation(shaderID, "model"),        1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(glGetUniformLocation(shaderID, "normalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMat));

            // Destaque sutil no objeto selecionado
            glm::vec3 color = meshes[i].color;
            if (i == selectedObject)
                color = glm::mix(color, glm::vec3(1.0f, 1.0f, 0.3f), 0.12f);

            glUniform3fv(glGetUniformLocation(shaderID, "objectColor"), 1, glm::value_ptr(color));
            glUniform3fv(glGetUniformLocation(shaderID, "ka"),          1, glm::value_ptr(meshes[i].ka));
            glUniform3fv(glGetUniformLocation(shaderID, "kd"),          1, glm::value_ptr(meshes[i].kd));
            glUniform3fv(glGetUniformLocation(shaderID, "ks"),          1, glm::value_ptr(meshes[i].ks));
            glUniform1f (glGetUniformLocation(shaderID, "shininess"),   meshes[i].shininess);

            glBindVertexArray(meshes[i].VAO);

            // Solido com Phong
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDrawArrays(GL_TRIANGLES, 0, meshes[i].nVertices);

            // Wireframe sobreposto (opcional)
            if (wireframe)
            {
                glm::vec3 wc(0.85f);
                glUniform3fv(glGetUniformLocation(shaderID, "objectColor"), 1, glm::value_ptr(wc));
                glUniform3fv(glGetUniformLocation(shaderID, "ka"), 1, glm::value_ptr(glm::vec3(1.0f)));
                glUniform3fv(glGetUniformLocation(shaderID, "kd"), 1, glm::value_ptr(glm::vec3(0.0f)));
                glUniform3fv(glGetUniformLocation(shaderID, "ks"), 1, glm::value_ptr(glm::vec3(0.0f)));
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1.0f, -1.0f);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glDrawArrays(GL_TRIANGLES, 0, meshes[i].nVertices);
                glDisable(GL_POLYGON_OFFSET_LINE);
            }

            glBindVertexArray(0);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glfwSwapBuffers(window);
    }

    for (auto& m : meshes)
        if (m.VAO) glDeleteVertexArrays(1, &m.VAO);
    glDeleteProgram(shaderID);
    glfwTerminate();
    return 0;
}

// ===========================================================================
// Utilitario: imprime estado do material no console
// ===========================================================================
void printMaterial(int idx)
{
    if (idx < 0 || idx >= (int)meshes.size()) return;
    const Mesh& m = meshes[idx];
    const char* comp[] = { "ka (ambiente)", "kd (difuso)", "ks (especular)", "shininess" };
    cout << fixed << setprecision(3);
    cout << "--- Obj[" << idx << "]"
         << " | ka=(" << m.ka.r << "," << m.ka.g << "," << m.ka.b << ")"
         << " kd=(" << m.kd.r << "," << m.kd.g << "," << m.kd.b << ")"
         << " ks=(" << m.ks.r << "," << m.ks.g << "," << m.ks.b << ")"
         << " shi=" << m.shininess
         << " | editando: [" << comp[editComponent] << "]\n";
}

// ===========================================================================
// Callbacks
// ===========================================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);

    if (action != GLFW_PRESS) return;  // REPEAT tratado no game loop via glfwGetKey
    if (meshes.empty()) return;

    Mesh& sel = meshes[selectedObject];

    // ---- TAB: alterna objeto ----
    if (key == GLFW_KEY_TAB)
    {
        selectedObject = (selectedObject + 1) % (int)meshes.size();
        cout << "\n>> Selecionado: objeto " << selectedObject << "\n";
        printMaterial(selectedObject);
        return;
    }

    // ---- M: toggle modo material ----
    if (key == GLFW_KEY_M)
    {
        materialMode = !materialMode;
        cout << "\n[Modo material: " << (materialMode ? "ON" : "OFF") << "]\n";
        if (materialMode)
        {
            cout << "  1=ka  2=kd  3=ks  4=shininess\n";
            cout << "  Segure +/- para ajustar   M=sair\n";
            printMaterial(selectedObject);
        }
        return;
    }

    // ---- Dentro do modo material: 1-4 selecionam componente ----
    if (materialMode)
    {
        if (key == GLFW_KEY_1) { editComponent = 0; cout << "[editando: ka]\n"; }
        if (key == GLFW_KEY_2) { editComponent = 1; cout << "[editando: kd]\n"; }
        if (key == GLFW_KEY_3) { editComponent = 2; cout << "[editando: ks]\n"; }
        if (key == GLFW_KEY_4) { editComponent = 3; cout << "[editando: shininess]\n"; }
        // +/- sao tratados no game loop (continuo); teclas de numero mudam componente
        return; // bloqueia atalhos de transformacao enquanto no modo material
    }

    // ---- Rotacao do objeto selecionado ----
    const float rStep = 5.0f;
    if (key == GLFW_KEY_X) sel.rotation.x += rStep;
    if (key == GLFW_KEY_Y) sel.rotation.y += rStep;
    if (key == GLFW_KEY_Z) sel.rotation.z += rStep;

    // ---- Escala uniforme ----
    const float sStep = 0.05f;
    if (key == GLFW_KEY_R) sel.scale += glm::vec3(sStep);
    if (key == GLFW_KEY_F) sel.scale = glm::max(sel.scale - glm::vec3(sStep), glm::vec3(0.05f));

    // ---- Alternancia de projecao ----
    if (key == GLFW_KEY_O)
    {
        usePerspective = !usePerspective;
        cout << "[Projecao: " << (usePerspective ? "Perspectiva" : "Ortografica") << "]\n";
    }

    // ---- Wireframe toggle ----
    if (key == GLFW_KEY_B)
    {
        wireframe = !wireframe;
        cout << "[Wireframe: " << (wireframe ? "ON" : "OFF") << "]\n";
    }

    // ---- Reset de transformacoes ----
    if (key == GLFW_KEY_BACKSPACE)
    {
        sel.rotation = glm::vec3(0.0f);
        sel.scale    = glm::vec3(1.0f);
        cout << "[Objeto " << selectedObject << " resetado]\n";
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoff =  (float)xpos - lastX;
    float yoff =  lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    camera.processMouseMovement(xoff, yoff);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.movementSpeed = glm::clamp(camera.movementSpeed + (float)yoffset * 0.5f, 1.0f, 20.0f);
}

// ===========================================================================
// Compilacao de Shaders
// ===========================================================================
GLuint setupShaders()
{
    auto compile = [](GLenum type, const GLchar* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; GLchar log[512];
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(s, 512, nullptr, log); cerr << "Shader error:\n" << log; }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   vertexShaderSource);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; GLchar log[512];
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, 512, nullptr, log); cerr << "Link error:\n" << log; }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}
