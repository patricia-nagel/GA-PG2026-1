// =============================================================================
// Camera.cpp — Implementação da câmera FPS com ângulos de Euler
// =============================================================================

#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp> // glm::lookAt, glm::radians, glm::cross, glm::normalize
#include <algorithm>                     // std::clamp (alternativa ao if/else para limitar pitch)

// -----------------------------------------------------------------------------
// Construtor
// -----------------------------------------------------------------------------
// A "initializer list" (após o ':') inicializa membros ANTES do corpo {}.
// É mais eficiente que atribuir dentro do {}, especialmente para objetos complexos.
Camera::Camera(glm::vec3 startPos, glm::vec3 startUp, float startYaw, float startPitch)
    : front(glm::vec3(0.0f, 0.0f, -1.0f)), // vetor inicial: câmera aponta para -Z
      movementSpeed(5.0f),                  // 5 unidades/segundo
      mouseSensitivity(0.1f)                // escala de 10%
{
    position = startPos;  // posição inicial no mundo
    worldUp  = startUp;   // eixo "cima" do mundo = (0,1,0)
    yaw      = startYaw;  // ângulo horizontal inicial (ex: -90° = aponta para -Z)
    pitch    = startPitch; // ângulo vertical inicial (0° = olha reto)

    // Calcula front, right e up a partir dos ângulos ANTES do primeiro frame.
    // Sem isso, os vetores estariam zerados na primeira chamada a getViewMatrix().
    updateCameraVectors();
}

// -----------------------------------------------------------------------------
// getViewMatrix() — Constrói e retorna a Matriz View
// -----------------------------------------------------------------------------
// CONCEITO — O que é a Matriz View?
//   Ela transforma todas as coordenadas do "espaço do mundo" para o
//   "espaço da câmera". No espaço da câmera, a câmera fica na origem
//   olhando para -Z, com Y apontando para cima.
//
//   É como se você "virasse o mundo inteiro" de forma que a câmera
//   fique parada e tudo mais se mova na direção oposta.
//
// glm::lookAt(eye, center, up) recebe:
//   eye    = posição da câmera no mundo
//   center = ponto para onde a câmera aponta (posição + direção)
//   up     = vetor "para cima" da câmera
glm::mat4 Camera::getViewMatrix()
{
    // "center" é calculado como posição + vetor front.
    // Não é um ponto fixo — a câmera sempre olha NA DIREÇÃO de front,
    // não para um objeto específico. Isso permite que ela "siga" o mouse.
    return glm::lookAt(position, position + front, up);
}

// -----------------------------------------------------------------------------
// processKeyboard() — Move a câmera com base na tecla pressionada
// -----------------------------------------------------------------------------
// CONCEITO — Por que multiplicar por deltaTime?
//   deltaTime = tempo que o frame anterior levou em segundos.
//   Sem multiplicar: em 120fps a câmera move 2× mais rápido que em 60fps.
//   Com multiplicar: a câmera move `movementSpeed` unidades POR SEGUNDO,
//   independente de quantos frames por segundo o programa está rodando.
void Camera::processKeyboard(const std::string& direction, float deltaTime)
{
    // velocity = distância a percorrer NESTE frame
    // Exemplo: movementSpeed=5, deltaTime=0.016s (60fps) → velocity=0.08 unidades
    float velocity = movementSpeed * deltaTime;

    // Avança na direção em que a câmera aponta (vetor front).
    if (direction == "FORWARD")  position += front * velocity;

    // Recua na direção oposta ao front.
    if (direction == "BACKWARD") position -= front * velocity;

    // Strafe para a esquerda: move perpendicular à direção de visão.
    // O vetor "right" aponta para a direita da câmera.
    if (direction == "LEFT")     position -= right * velocity;

    // Strafe para a direita.
    if (direction == "RIGHT")    position += right * velocity;

    // Sobe no eixo Y da câmera (não do mundo — permite câmera inclinada).
    if (direction == "UP")       position += up * velocity;

    // Desce.
    if (direction == "DOWN")     position -= up * velocity;
}

// -----------------------------------------------------------------------------
// processMouseMovement() — Rotaciona a câmera com base no movimento do mouse
// -----------------------------------------------------------------------------
// xoffset = pixels que o mouse moveu horizontalmente desde o último frame
// yoffset = pixels que o mouse moveu verticalmente (JÁ INVERTIDO pelo chamador:
//           tela cresce para baixo, mas pitch positivo = olhar para cima)
void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
    // Escala os offsets pela sensibilidade.
    // mouseSensitivity=0.1 → 10 pixels de movimento = 1 grau de rotação.
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    // Yaw: rotação horizontal. Mouse para a direita → yaw aumenta → câmera gira à direita.
    yaw   += xoffset;

    // Pitch: rotação vertical. Mouse para cima → yoffset positivo → pitch aumenta → câmera olha para cima.
    pitch += yoffset;

    // Limita o pitch para evitar "gimbal lock":
    // Se pitch chegar a ±90°, a câmera ficaria olhando direto para cima/baixo,
    // e o vetor "up" ficaria indefinido → a cena "viraria".
    // Limitamos a ±89° como margem de segurança.
    if (constrainPitch)
    {
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    // Recalcula os vetores front, right e up com os novos ângulos.
    updateCameraVectors();
}

// -----------------------------------------------------------------------------
// updateCameraVectors() — Converte ângulos de Euler em vetores de direção
// -----------------------------------------------------------------------------
// CONCEITO — Ângulos de Euler para vetores (trigonometria esférica):
//
//   O "front" é um ponto na superfície de uma esfera unitária.
//   Yaw gira em torno do eixo Y (horizontal), Pitch inclina no eixo X (vertical).
//
//   Fórmulas:
//     front.x = cos(yaw) * cos(pitch)  — componente horizontal no eixo X
//     front.y = sin(pitch)              — componente vertical
//     front.z = sin(yaw) * cos(pitch)  — componente horizontal no eixo Z
//
//   Exemplo: yaw=-90°, pitch=0°
//     front.x = cos(-90°)*cos(0°) = 0*1 = 0
//     front.y = sin(0°)            = 0
//     front.z = sin(-90°)*cos(0°) = -1*1 = -1
//     → front = (0, 0, -1) = câmera olhando para -Z ✓
void Camera::updateCameraVectors()
{
    glm::vec3 newFront;

    // Calcula as componentes do vetor front a partir dos ângulos yaw e pitch.
    // glm::radians() converte graus para radianos.
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

    // Normaliza para garantir comprimento 1 (necessário para lookAt e cross product).
    front = glm::normalize(newFront);

    // right = cross(front, worldUp)
    // O produto vetorial de dois vetores é perpendicular a ambos.
    // front × worldUp(0,1,0) = vetor apontando para a direita da câmera.
    right = glm::normalize(glm::cross(front, worldUp));

    // up = cross(right, front)
    // O "cima real" da câmera. Normalmente próximo a (0,1,0),
    // mas difere se a câmera estiver inclinada.
    up = glm::normalize(glm::cross(right, front));
}