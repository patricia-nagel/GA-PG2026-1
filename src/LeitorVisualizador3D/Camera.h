// =============================================================================
// Camera.h — Declaração da classe de câmera FPS (First Person Shooter)
// =============================================================================
// PAPEL DA CÂMERA NO PIPELINE:
//   A câmera define a Matriz View (Visualização).
//   Ela responde à pergunta: "de onde estamos olhando a cena, e para onde?"
//
//   A Matriz View transforma coordenadas do "espaço do mundo" para o
//   "espaço da câmera" (onde a câmera está na origem, olhando para -Z).
//
//   A câmera em si NUNCA se move no OpenGL. O que fazemos é mover o mundo
//   inteiro na direção OPOSTA ao movimento da câmera. A Matriz View faz isso.
// =============================================================================

// Guard de inclusão — evita redefinição da classe se o header for incluído 2x
#ifndef CAMERA_H
#define CAMERA_H

// GLM: tipos matemáticos (vec3, mat4) e função lookAt
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// std::string — usado no método processKeyboard para receber "FORWARD", etc.
#include <string>

class Camera {
public:
    // -------------------------------------------------------------------------
    // VETORES DE ESTADO — descrevem a orientação da câmera no espaço
    // -------------------------------------------------------------------------

    // Posição da câmera no espaço do mundo (X, Y, Z).
    glm::vec3 position;

    // Vetor unitário que aponta para onde a câmera está olhando.
    // Exemplo: front=(0,0,-1) = câmera olhando para "dentro" da tela.
    glm::vec3 front;

    // Vetor "para cima" DA CÂMERA (pode mudar se a câmera inclinar).
    // Recalculado por updateCameraVectors() a cada mudança de ângulo.
    glm::vec3 up;

    // Vetor "para a direita" da câmera.
    // Calculado como cross(front, worldUp). Usado para strafe (A/D).
    glm::vec3 right;

    // Vetor "para cima" DO MUNDO — sempre (0, 1, 0).
    // Referência fixa. Necessário para calcular "right" corretamente.
    glm::vec3 worldUp;

    // -------------------------------------------------------------------------
    // ÂNGULOS DE EULER — representação da rotação da câmera
    // -------------------------------------------------------------------------
    // Guardamos a rotação como 2 ângulos (mais simples que quaternions para câmera FPS).
    // updateCameraVectors() converte esses ângulos para os vetores front/right/up.

    // Yaw = guinada = rotação horizontal (esquerda/direita).
    // -90.0f = câmera aponta para -Z (frente da cena quando o objeto está na origem).
    float yaw;

    // Pitch = arfagem = rotação vertical (para cima/baixo).
    // 0.0f = câmera olha reto para frente. Limitado entre -89 e +89 graus.
    float pitch;

    // -------------------------------------------------------------------------
    // CONFIGURAÇÕES DE MOVIMENTO
    // -------------------------------------------------------------------------

    // Velocidade de movimento em unidades do mundo por segundo.
    // Multiplicada por deltaTime em processKeyboard() para movimento frame-rate independente.
    float movementSpeed;

    // Fator de escala dos offsets de mouse.
    // Valor pequeno (0.1) = movimento suave. Valor grande = câmera "solta demais".
    float mouseSensitivity;

    // -------------------------------------------------------------------------
    // INTERFACE PÚBLICA
    // -------------------------------------------------------------------------

    // Construtor com valores padrão.
    Camera(glm::vec3 startPos = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 startUp  = glm::vec3(0.0f, 1.0f, 0.0f),
           float startYaw     = -90.0f,
           float startPitch   = 0.0f);

    // Retorna a Matriz View calculada com a posição e direção atuais.
    // Chamada a cada frame no game loop: view = camera.getViewMatrix()
    glm::mat4 getViewMatrix();

    // Processa teclas WASD/QE para mover a câmera.
    // deltaTime garante velocidade constante independente do framerate.
    void processKeyboard(const std::string& direction, float deltaTime);

    // Processa o movimento do mouse para rotacionar a câmera (yaw/pitch).
    // xoffset = quanto o mouse moveu horizontalmente desde o último frame.
    // yoffset = quanto o mouse moveu verticalmente (invertido: tela cresce para baixo).
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

private:
    // Recalcula os vetores front, right e up a partir dos ângulos yaw e pitch.
    // Chamada sempre que yaw ou pitch mudam.
    void updateCameraVectors();
};

#endif // CAMERA_H