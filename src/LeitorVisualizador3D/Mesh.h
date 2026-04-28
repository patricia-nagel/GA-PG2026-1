// =============================================================================
// Mesh.h — Estrutura de dados que representa um objeto 3D na cena
// =============================================================================
// ONDE VIVE: 100% na CPU (RAM). O campo VAO é apenas um número inteiro que
// serve de "endereço" para acessar a geometria que já está na GPU (VRAM).
// A CPU nunca toca diretamente nos dados de vértice depois de enviá-los.
// =============================================================================

// Guard de inclusão: se este header já foi incluído em algum .cpp,
// o preprocessador ignora o conteúdo na segunda inclusão.
// Evita erros de "redefinição de struct".
#ifndef MESH_H
#define MESH_H

// GLuint é o tipo inteiro sem sinal do OpenGL. Definido pelo GLAD.
// Precisamos dele para declarar o campo VAO abaixo.
#include <glad/glad.h>

// vec3 (x,y,z), vec4, mat4 — tipos matemáticos da biblioteca GLM.
// GLM é a "matemática 3D" do nosso programa: vetores, matrizes, etc.
#include <glm/glm.hpp>

// glm::translate, glm::rotate, glm::scale — funções que constroem
// matrizes de transformação. Usadas em getModelMatrix().
#include <glm/gtc/matrix_transform.hpp>

// std::string — para guardar o nome/caminho do arquivo .obj
#include <string>

// -----------------------------------------------------------------------------
// struct Mesh
// "struct" em C++ é como "class" mas com todos os membros públicos por padrão.
// Cada instância desta struct representa UM objeto na cena.
// -----------------------------------------------------------------------------
struct Mesh
{
    // -------------------------------------------------------------------------
    // IDENTIFICADOR NA GPU
    // -------------------------------------------------------------------------

    // VAO = Vertex Array Object.
    // É um número inteiro gerado pelo OpenGL (ex: 1, 2, 3...).
    // Ele "aponta" para a configuração de geometria que está na VRAM (memória da GPU).
    // Valor 0 = ainda não foi criado / falhou ao carregar.
    // Na hora de desenhar: glBindVertexArray(VAO) → diz à GPU qual geometria usar.
    GLuint VAO = 0;

    // Quantidade de vértices carregados do arquivo .obj.
    // Passado como 3º argumento para glDrawArrays(GL_TRIANGLES, 0, nVertices).
    int nVertices = 0;

    // Nome/caminho do arquivo .obj — útil para mensagens de debug no console.
    std::string name;

    // -------------------------------------------------------------------------
    // TRANSFORMAÇÕES DO OBJETO (ficam na CPU, calculadas a cada frame)
    // -------------------------------------------------------------------------
    // Estas três propriedades definem "onde e como" o objeto aparece no mundo.
    // A cada frame, getModelMatrix() as combina em uma única matriz 4x4
    // que é enviada para a GPU via glUniformMatrix4fv.

    // Posição do objeto no espaço do mundo (X, Y, Z).
    // glm::vec3(0.0f) = na origem (0,0,0).
    glm::vec3 position = glm::vec3(0.0f);

    // Rotação em GRAUS nos eixos X, Y e Z.
    // Exemplo: rotation.y = 45.0f → objeto girado 45° no eixo Y.
    // Guardamos em graus por ser mais intuitivo; convertemos para radianos em getModelMatrix().
    glm::vec3 rotation = glm::vec3(0.0f);

    // Escala nos eixos X, Y e Z.
    // glm::vec3(1.0f) = tamanho original.
    // glm::vec3(2.0f) = dobro do tamanho em todos os eixos.
    glm::vec3 scale = glm::vec3(1.0f);

    // -------------------------------------------------------------------------
    // PROPRIEDADES DE MATERIAL — MODELO DE PHONG
    // -------------------------------------------------------------------------
    // O modelo de Phong divide a iluminação em 3 partes independentes.
    // Cada coeficiente (ka, kd, ks) é um vec3 (R,G,B) que controla
    // quanto de cada componente de cor o material reflete.
    // Esses valores são enviados para o Fragment Shader via glUniform3fv a cada frame,
    // por isso podem ser alterados em tempo real (modo M do programa).

    // ka = coeficiente AMBIENTE.
    // Controla quanto de luz indireta (luz de preenchimento) o material reflete.
    // Valor baixo (0.1) = superfície quase preta sem luz direta.
    // Valor alto (0.5+) = superfície sempre visível mesmo em sombra total.
    glm::vec3 ka = glm::vec3(0.1f);

    // kd = coeficiente DIFUSO.
    // Controla a iluminação principal: quanto de luz direta o material espalha.
    // Depende do ângulo entre a normal da superfície e a direção da luz (Lei de Lambert).
    // Valor alto (0.8) = superfície bem iluminada quando voltada para a luz.
    glm::vec3 kd = glm::vec3(0.8f);

    // ks = coeficiente ESPECULAR.
    // Controla o brilho "brilhante" (highlight especular).
    // Depende do ângulo entre o observador e o reflexo da luz na superfície.
    glm::vec3 ks = glm::vec3(0.5f);

    // shininess = expoente especular.
    // Controla o TAMANHO do brilho especular.
    // Valor baixo (1-8)   = brilho grande, espalhado (plástico fosco).
    // Valor alto (64-256) = brilho pequeno, concentrado (metal polido, espelho).
    float shininess = 32.0f;

    // Cor base do objeto (RGB, cada canal entre 0.0 e 1.0).
    // No Fragment Shader, o resultado do cálculo de Phong é MULTIPLICADO por essa cor.
    // Exemplo: glm::vec3(0.8, 0.2, 0.2) = objeto avermelhado.
    glm::vec3 color = glm::vec3(1.0f, 0.5f, 0.2f);

    // -------------------------------------------------------------------------
    // getModelMatrix() — Monta a Matriz Model deste objeto
    // -------------------------------------------------------------------------
    // A Matriz Model é uma matriz 4x4 que descreve TODAS as transformações
    // do objeto: onde ele está, para onde aponta e qual seu tamanho.
    //
    // CONCEITO IMPORTANTE — Espaço do Objeto vs Espaço do Mundo:
    //   • Os vértices no arquivo .obj estão em "espaço do objeto" (coordenadas
    //     locais, centradas na origem do objeto, ex: vértice (0.5, 0, 0)).
    //   • A Matriz Model converte essas coordenadas para "espaço do mundo"
    //     (posição real na cena).
    //   • Exemplo: objeto com position=(3,0,0) → vértice (0.5,0,0) vai para (3.5,0,0).
    //
    // A função é marcada "const" pois não modifica nenhum membro da struct.
    glm::mat4 getModelMatrix() const
    {
        // Começa com a matriz identidade (equivalente a "sem transformação").
        // mat4(1.0f) = diagonal principal = 1, resto = 0.
        glm::mat4 model = glm::mat4(1.0f);

        // PASSO 1 — Translação: move o objeto para sua posição no mundo.
        // A matriz resultante "desloca" todos os vértices por (position.x, position.y, position.z).
        model = glm::translate(model, position);

        // PASSO 2 — Rotação nos 3 eixos.
        // glm::radians() converte graus → radianos (OpenGL/GLM usam radianos).
        // O segundo argumento é o eixo de rotação como vetor unitário.
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)); // eixo X
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)); // eixo Y
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f)); // eixo Z

        // PASSO 3 — Escala: multiplica as coordenadas dos vértices pelos fatores de escala.
        // scale=(2,1,1) → dobra a largura mas mantém altura e profundidade.
        model = glm::scale(model, scale);

        // Retorna a matriz final (4x4 de floats).
        // Será enviada à GPU no game loop via:
        //   glUniformMatrix4fv(glGetUniformLocation(shaderID, "model"), 1, GL_FALSE, glm::value_ptr(model))
        return model;
    }
};

#endif // MESH_H