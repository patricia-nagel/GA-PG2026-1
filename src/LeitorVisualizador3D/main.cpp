/*
 * =============================================================================
 * main.cpp — Programa principal do Visualizador 3D
 * Trabalho Grau A — Processamento Gráfico / Computação Gráfica — Unisinos
 * =============================================================================
 *
 * ARQUITETURA GERAL DO PROGRAMA:
 *
 *  ┌─────────────────────────────────────────────────────────────────────┐
 *  │  CPU (seu programa C++)         │  GPU (placa de vídeo)             │
 *  │─────────────────────────────────│───────────────────────────────────│
 *  │  main()                         │                                   │
 *  │   └─ loadSimpleOBJ()            │                                   │
 *  │       └─ glBufferData() ────────┼──► VBO (vértices na VRAM)         │
 *  │       └─ glVertexAttribPointer()┼──► VAO (mapa de leitura do VBO)   │
 *  │                                 │                                   │
 *  │   └─ setupShaders() ────────────┼──► Vertex Shader compilado na GPU │
 *  │                                 │    Fragment Shader compilado GPU  │
 *  │                                 │                                   │
 *  │  [game loop, todo frame]:       │                                   │
 *  │   └─ GLM calcula matrizes       │                                   │
 *  │   └─ glUniform*() ──────────────┼──► uniforms (mat4 model/view/proj)│
 *  │   └─ glDrawArrays() ────────────┼──► PIPELINE:                      │
 *  │                                 │    1. Vertex Shader (por vértice) │
 *  │                                 │    2. Rasterização                │
 *  │                                 │    3. Fragment Shader (por pixel) │
 *  │                                 │    4. Depth Test                  │
 *  │                                 │    5. Framebuffer → tela          │
 *  └─────────────────────────────────┴───────────────────────────────────┘
 */

// =============================================================================
// INCLUDES — Bibliotecas necessárias
// =============================================================================

#include <iostream>  // cout (saída padrão), cerr (saída de erro)
#include <iomanip>   // setprecision() — formatar decimais no console
#include <string>    // std::string
#include <vector>    // std::vector — array dinâmico, guarda os 3 objetos (meshes)
#include <fstream>   // ifstream — leitura de arquivos (usado em LoadOBJ.h)
#include <sstream>   // istringstream — parsing de strings (usado em LoadOBJ.h)

using namespace std; // Evita prefixo std:: em cout, vector, string, etc.

// GLAD: carrega os ponteiros de funções do OpenGL em tempo de execução.
// DEVE ser incluído ANTES do GLFW. Sem o GLAD, funções como glGenBuffers
// não existem no executável.
#include <glad/glad.h>

// GLFW: cria a janela, o contexto OpenGL e processa eventos de teclado/mouse.
#include <GLFW/glfw3.h>

// GLM: biblioteca matemática para vetores e matrizes.
#include <glm/glm.hpp>                  // tipos: vec2, vec3, vec4, mat3, mat4
#include <glm/gtc/matrix_transform.hpp> // glm::translate, rotate, scale, perspective, ortho, lookAt
#include <glm/gtc/type_ptr.hpp>         // glm::value_ptr() — ponteiro para o array interno do GLM

// Nossas classes/structs
#include "Camera.h"  // Câmera FPS com ângulos de Euler
#include "Mesh.h"    // Struct de objeto 3D (VAO + transformações + material Phong)
#include "LoadOBJ.h" // Parser .obj → VBO/VAO

// =============================================================================
// SHADERS GLSL — código que roda NA GPU
// =============================================================================
// R"glsl(...)glsl" é uma raw string literal do C++11: permite escrever o código
// GLSL diretamente aqui, sem escapar aspas ou quebras de linha.
// Esses strings são compilados na GPU durante setupShaders(), em tempo de execução.

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
// Roda UMA VEZ POR VÉRTICE na GPU (chamado tantas vezes quanto nVertices).
// Sua principal responsabilidade: transformar a posição do vértice de
// "espaço do objeto" para "espaço de clip" (coordenadas normalizadas da tela).
// Também prepara os dados que serão interpolados e passados ao Fragment Shader.
const GLchar* vertexShaderSource = R"glsl(
#version 330 core
// 330 = OpenGL 3.3. "core" = sem funções depreciadas.

// ENTRADAS (atributos de vértice — lidos do VBO via VAO):
// O número em location= DEVE bater com o glVertexAttribPointer(N, ...) em LoadOBJ.h
layout(location = 0) in vec3 aPos;      // Posição do vértice (x,y,z) — layout 0
layout(location = 1) in vec3 aNormal;   // Normal do vértice (nx,ny,nz) — layout 1
layout(location = 2) in vec2 aTexCoord; // Coord de textura (s,t) — layout 2

// UNIFORMS: constantes para todos os vértices do mesmo drawcall.
// Enviadas pela CPU via glUniformMatrix4fv() a cada frame.
uniform mat4 model;        // Transforma: espaço do objeto → espaço do mundo
uniform mat4 view;         // Transforma: espaço do mundo → espaço da câmera
uniform mat4 projection;   // Transforma: espaço da câmera → espaço de clip (perspectiva/ortho)
uniform mat3 normalMatrix; // Transforma normais corretamente: transpose(inverse(mat3(model)))
                           // Necessária pois escala não-uniforme distorce normais se usarmos mat3(model)

// SAÍDAS: passadas ao Fragment Shader (interpoladas entre os vértices do triângulo)
out vec3 FragPos;   // Posição do fragmento no espaço do mundo
out vec3 Normal;    // Normal transformada no espaço do mundo
out vec2 TexCoord;  // Coordenada de textura (passa direto, sem transformação)

void main()
{
    // PASSO 1: transforma o vértice para o espaço do mundo.
    // vec4(aPos, 1.0) → w=1.0 indica que é um PONTO (não um vetor, que seria w=0.0)
    vec4 worldPos = model * vec4(aPos, 1.0);

    // FragPos = posição no mundo, em vec3 (descartando w).
    // Usada no Fragment Shader para calcular a direção até a luz e até a câmera.
    FragPos = vec3(worldPos);

    // Transforma a normal pela normalMatrix (não pela model!).
    // Se model tiver escala não-uniforme, usar mat3(model) distorceria as normais.
    // normalMatrix = transpose(inverse(mat3(model))) corrige isso.
    Normal = normalMatrix * aNormal;

    // TexCoord não precisa de transformação: vai direto para o Fragment Shader.
    TexCoord = aTexCoord;

    // POSIÇÃO FINAL DO VÉRTICE NA TELA.
    // Multiplicação de matrizes: lida da direita para a esquerda:
    //   1. worldPos já está no espaço do mundo (model já foi aplicada acima)
    //   2. view: leva para o espaço da câmera
    //   3. projection: aplica perspectiva (objetos distantes parecem menores)
    // Resultado em "clip coordinates" — a GPU divide por w para projetar na tela.
    gl_Position = projection * view * worldPos;
}
)glsl";

// -----------------------------------------------------------------------------
// FRAGMENT SHADER — Modelo de Iluminação de Phong
// -----------------------------------------------------------------------------
// Roda UMA VEZ POR FRAGMENTO (pixel candidato) na GPU.
// Recebe os valores interpolados do Vertex Shader e calcula a cor final do pixel.
//
// O modelo de Phong divide a iluminação em 3 componentes:
//   Ambiente  = luz indireta constante (evita sombras totalmente pretas)
//   Difusa    = luz espalhada pela superfície (depende do ângulo com a luz)
//   Especular = reflexo brilhante (depende do ângulo entre observador e reflexo)
const GLchar* fragmentShaderSource = R"glsl(
#version 330 core

// Entradas interpoladas do Vertex Shader
in vec3 FragPos;   // Posição deste fragmento no mundo
in vec3 Normal;    // Normal interpolada (pode não ser mais unitária após interpolação)
in vec2 TexCoord;  // Coord de textura (não usada neste shader, mas disponível)

// Saída: cor RGBA final deste fragmento (pixel)
out vec4 FragColor;

// Posição da câmera no mundo — para calcular o vetor "V" (fragmento → câmera)
uniform vec3 viewPos;

// Propriedades da luz pontual (única fonte de luz na cena)
uniform vec3 lightPos;   // Posição da luz no mundo
uniform vec3 lightColor; // Cor/intensidade da luz (normalmente branco: 1,1,1)

// Propriedades do material — AJUSTÁVEIS EM TEMPO REAL via teclado (modo M)
// Enviadas a cada frame via glUniform3fv / glUniform1f
uniform vec3  ka;          // Coeficiente ambiente: quanto o material reflete de luz indireta
uniform vec3  kd;          // Coeficiente difuso: quanto de luz direta o material espalha
uniform vec3  ks;          // Coeficiente especular: intensidade do brilho
uniform float shininess;   // Expoente especular: tamanho do brilho (alto = pequeno e concentrado)
uniform vec3  objectColor; // Cor base do objeto (multiplicada no resultado final)

void main()
{
    // ---- Vetores necessários para o cálculo de Phong ----

    // N: normal da superfície neste ponto. Renormalizada porque a interpolação
    // entre vértices pode alterar o comprimento (o vetor deixa de ser unitário).
    vec3 norm = normalize(Normal);

    // L: vetor da superfície ATÉ a luz. Normalizado (comprimento = 1).
    // lightPos - FragPos aponta "da superfície para a luz".
    vec3 lightDir = normalize(lightPos - FragPos);

    // V: vetor da superfície ATÉ a câmera.
    // Necessário para o cálculo especular (o brilho depende de onde você está olhando).
    vec3 viewDir = normalize(viewPos - FragPos);

    // R: reflexo do vetor de luz em torno da normal.
    // reflect() espera o vetor INCIDENTE (que vem da luz, sentido oposto ao lightDir),
    // por isso usamos -lightDir.
    // R é usado para verificar se o observador está na direção do reflexo.
    vec3 reflDir = reflect(-lightDir, norm);

    // ---- COMPONENTE AMBIENTE ----
    // Luz de preenchimento: constante, não depende de nenhum ângulo.
    // Garante que o objeto nunca fica completamente preto mesmo sem luz direta.
    // Fórmula: ka × Iambiente
    vec3 ambient = ka * lightColor;

    // ---- COMPONENTE DIFUSA (Lei de Lambert) ----
    // A intensidade difusa é proporcional ao cosseno do ângulo entre N e L.
    // dot(N, L) = cos(ângulo). Quanto mais a superfície "olha" para a luz, mais iluminada.
    // max(..., 0.0) garante que luz "atrás" da superfície não resulta em valor negativo.
    float diff   = max(dot(norm, lightDir), 0.0);
    // Fórmula: kd × diff × Idifusa
    vec3 diffuse = kd * diff * lightColor;

    // ---- COMPONENTE ESPECULAR (Phong) ----
    // dot(V, R): quão próximo o observador está da direção de reflexo.
    // pow(..., shininess): expoente controla o tamanho do brilho.
    //   shininess alto (128): brilho pequeno e intenso (espelho polido)
    //   shininess baixo (4):  brilho grande e espalhado (plástico fosco)
    float spec    = pow(max(dot(viewDir, reflDir), 0.0), shininess);
    // Fórmula: ks × spec × Iespecular
    vec3 specular = ks * spec * lightColor;

    // ---- RESULTADO FINAL ----
    // Soma as 3 componentes e multiplica pela cor do objeto.
    // A cor do objeto "filtra" a luz: um objeto vermelho reflete só o canal vermelho.
    vec3 result = (ambient + diffuse + specular) * objectColor;

    // FragColor: saída RGBA. Alpha = 1.0 = completamente opaco.
    FragColor = vec4(result, 1.0);
}
)glsl";

// =============================================================================
// VARIÁVEIS GLOBAIS
// =============================================================================
// São globais pois os callbacks de teclado/mouse precisam acessá-las
// sem poder receber parâmetros adicionais (assinatura fixada pelo GLFW).

const GLuint WIDTH = 800, HEIGHT = 600; // Dimensões da janela em pixels

// Câmera: posição inicial em Z=5 (atrás da cena), worldUp=(0,1,0), yaw=-90°, pitch=0°.
// yaw=-90° faz front=(0,0,-1) → câmera aponta para dentro da cena.
Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

// Controle de tempo entre frames.
// deltaTime = duração do frame anterior em segundos.
// Multiplicar velocidades por deltaTime garante movimento frame-rate independente.
float deltaTime = 0.0f;
float lastFrame = 0.0f; // timestamp do frame anterior

// Rastreamento da posição do mouse para calcular o offset (quanto moveu).
float lastX      = WIDTH  / 2.0f; // posição X do mouse no frame anterior
float lastY      = HEIGHT / 2.0f; // posição Y do mouse no frame anterior
bool  firstMouse = true; // evita pulo de câmera na primeira leitura do mouse

// Estado de controle da cena
int  selectedObject = 0;    // índice do objeto ativo (0, 1 ou 2)
bool wireframe      = false; // true = sobrepõe wireframe ao sólido
bool usePerspective = true;  // true = perspectiva | false = ortográfica

// Modo de edição de material (tecla M)
// editComponent: qual coeficiente está sendo ajustado
//   0 = ka (ambiente)
//   1 = kd (difuso)
//   2 = ks (especular)
//   3 = shininess
int  editComponent = 1;
bool materialMode  = false; // true = modo M ativo (+/- ajustam o material)

// Vetor com os 3 objetos da cena.
// Global para ser acessado tanto em main() quanto nos callbacks.
vector<Mesh> meshes;

// =============================================================================
// PROTÓTIPOS DE FUNÇÕES
// =============================================================================
// Necessários pois as funções são definidas DEPOIS de main(),
// mas main() precisa chamá-las.

GLuint setupShaders();   // Compila shaders GLSL e retorna o ID do programa
void   printMaterial(int idx); // Imprime coeficientes de material no console
void   key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void   mouse_callback(GLFWwindow* window, double xpos, double ypos);
void   scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// =============================================================================
// MAIN — Inicialização, carregamento e game loop
// =============================================================================
int main()
{
    // -------------------------------------------------------------------------
    // INICIALIZAÇÃO DO GLFW
    // -------------------------------------------------------------------------

    // Inicializa a biblioteca GLFW. Deve ser a primeira chamada GLFW.
    glfwInit();

    // Define a versão do OpenGL que queremos: 3.3 Core Profile.
    // Core Profile = API moderna, sem funções depreciadas das versões antigas.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Necessário para macOS (Apple usa OpenGL 4.1 mas exige FORWARD_COMPAT para Core Profile)
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Cria a janela com contexto OpenGL.
    // Parâmetros: largura, altura, título, monitor (nullptr=janela), janela pai (nullptr)
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "Visualizador 3D | TAB=Obj | WASD=Cam | M=Material | B=Wire | O=Proj",
        nullptr, nullptr);
    if (!window)
    {
        cerr << "Falha ao criar janela GLFW\n";
        glfwTerminate(); // Libera recursos do GLFW
        return -1;
    }

    // Vincula o contexto OpenGL desta janela à thread atual.
    // OBRIGATÓRIO antes de qualquer chamada a funções gl*.
    glfwMakeContextCurrent(window);

    // Registra as funções de callback.
    // GLFW chamará essas funções automaticamente quando os eventos ocorrerem.
    glfwSetKeyCallback(window, key_callback);       // teclado
    glfwSetCursorPosCallback(window, mouse_callback); // mouse move
    glfwSetScrollCallback(window, scroll_callback);   // scroll do mouse

    // Esconde o cursor e o "prende" dentro da janela.
    // Necessário para câmera FPS: sem isso o cursor sai da janela e para de enviar eventos.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // -------------------------------------------------------------------------
    // INICIALIZAÇÃO DO GLAD
    // -------------------------------------------------------------------------
    // GLAD carrega os ponteiros para as funções do OpenGL.
    // Sem isso, funções como glGenBuffers, glDrawArrays, etc. não existem.
    // glfwGetProcAddress fornece o endereço de cada função para o SO atual.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        cerr << "Falha ao inicializar GLAD\n";
        return -1;
    }

    // Obtém o tamanho real do framebuffer (pode diferir de WIDTH/HEIGHT em telas Retina/HiDPI)
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);

    // Define a área de renderização em pixels dentro da janela.
    glViewport(0, 0, fbW, fbH);

    // Informações de diagnóstico: qual GPU está sendo usada e versão do OpenGL
    cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << "\n\n";

    // -------------------------------------------------------------------------
    // CARREGAMENTO DOS 3 OBJETOS
    // -------------------------------------------------------------------------

    // Struct local para organizar as configurações iniciais de cada objeto.
    // Usada apenas para inicialização; não persiste depois do loop abaixo.
    struct ObjCfg {
        string    path;       // Caminho do arquivo .obj
        glm::vec3 pos;        // Posição inicial no mundo
        glm::vec3 color;      // Cor base (RGB)
        glm::vec3 ka, kd, ks; // Coeficientes de material Phong
        float     shininess;  // Expoente especular
    };

    // Configurações dos 3 objetos. EDITE os caminhos para seus .obj.
    // Os 3 usam o mesmo modelo mas com cores e materiais diferentes.
    vector<ObjCfg> configs = {
        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3(-2.5f, 0.0f, 0.0f),   // objeto da esquerda
          glm::vec3(0.85f, 0.20f, 0.20f), // vermelho
          glm::vec3(0.10f), glm::vec3(0.80f), glm::vec3(0.50f), 32.0f },

        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3( 0.0f, 0.0f, 0.0f),   // objeto do centro
          glm::vec3(0.20f, 0.80f, 0.25f), // verde
          glm::vec3(0.10f), glm::vec3(0.80f), glm::vec3(0.30f), 16.0f },

        { "../assets/Modelos3D/SuzanneSubdiv1.obj",
          glm::vec3( 2.5f, 0.0f, 0.0f),   // objeto da direita
          glm::vec3(0.20f, 0.40f, 0.90f), // azul
          glm::vec3(0.20f), glm::vec3(0.70f), glm::vec3(0.90f), 128.0f },
    };

    for (auto& cfg : configs)
    {
        Mesh m; // Cria um Mesh vazio (VAO=0, nVertices=0)

        // loadSimpleOBJ lê o .obj, cria VBO+VAO na GPU, e preenche m.VAO e m.nVertices.
        // Se o arquivo não existir, m.VAO permanece 0 (verificado no loop de desenho).
        loadSimpleOBJ(cfg.path, m);

        // Configura as propriedades do objeto (ficam na CPU, enviadas como uniforms a cada frame)
        m.position  = cfg.pos;
        m.color     = cfg.color;
        m.ka        = cfg.ka;
        m.kd        = cfg.kd;
        m.ks        = cfg.ks;
        m.shininess = cfg.shininess;

        // push_back COPIA o Mesh para o vetor global.
        // A partir daqui, usamos meshes[0], meshes[1], meshes[2].
        meshes.push_back(m);
    }

    cout << "\n=== " << meshes.size() << " objetos carregados ===\n";
    cout << "TAB=selecionar | M=material | B=wireframe | O=projecao\n";
    cout << "No modo M: 1=ka  2=kd  3=ks  4=shininess  +/-=ajustar\n\n";
    printMaterial(selectedObject); // Mostra o material do objeto inicial no console

    // -------------------------------------------------------------------------
    // COMPILAÇÃO DOS SHADERS
    // -------------------------------------------------------------------------

    // Compila vertexShaderSource e fragmentShaderSource na GPU,
    // linka em um programa e retorna o ID do programa.
    GLuint shaderID = setupShaders();

    // Ativa o programa de shader. Todas as chamadas glUniform* seguintes
    // afetam ESTE programa.
    glUseProgram(shaderID);

    // Ativa o teste de profundidade (Z-buffer).
    // A GPU descarta automaticamente fragmentos que estão "atrás" de outros já desenhados.
    // Sem isso, objetos desenhados por último sempre aparecem na frente (independente da distância).
    glEnable(GL_DEPTH_TEST);

    // Configura a luz pontual (posição e cor fixas — enviadas apenas uma vez)
    glm::vec3 lightPos   = glm::vec3(3.0f, 5.0f, 3.0f); // acima e à frente/direita da cena
    glm::vec3 lightColor = glm::vec3(1.0f);              // luz branca

    // glGetUniformLocation: busca o índice da variável "lightPos" no programa de shader.
    // glUniform3fv: envia o vec3 para a GPU.
    //   "3fv" = 3 floats, vetor. O "1" = quantos vetores estamos enviando.
    //   glm::value_ptr() retorna ponteiro para o array interno do GLM.
    glUniform3fv(glGetUniformLocation(shaderID, "lightPos"),   1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(shaderID, "lightColor"), 1, glm::value_ptr(lightColor));

    // ==========================================================================
    // GAME LOOP — roda continuamente até o usuário fechar a janela
    // ==========================================================================
    while (!glfwWindowShouldClose(window))
    {
        // ----------------------------------------------------------------------
        // CONTROLE DE TEMPO
        // ----------------------------------------------------------------------

        float now = (float)glfwGetTime(); // segundos desde o início do programa
        deltaTime = now - lastFrame;      // tempo que o frame anterior levou
        lastFrame = now;                  // atualiza para o próximo frame

        // ----------------------------------------------------------------------
        // PROCESSAMENTO DE EVENTOS
        // ----------------------------------------------------------------------
        // Verifica eventos pendentes (teclado, mouse, fechar janela) e
        // chama os callbacks correspondentes registrados em glfwSet*Callback().
        glfwPollEvents();

        // ----------------------------------------------------------------------
        // MOVIMENTO CONTÍNUO DA CÂMERA (verificado a cada frame)
        // ----------------------------------------------------------------------
        // glfwGetKey verifica o ESTADO ATUAL da tecla (pressionada ou não).
        // Diferente do key_callback que só dispara no EVENTO (pressionar/soltar).
        // Isso permite movimento suave enquanto a tecla está mantida pressionada.
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.processKeyboard("FORWARD",  deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.processKeyboard("BACKWARD", deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.processKeyboard("LEFT",     deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.processKeyboard("RIGHT",    deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.processKeyboard("UP",       deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.processKeyboard("DOWN",     deltaTime);

        // ----------------------------------------------------------------------
        // AJUSTE CONTÍNUO DE MATERIAL (só quando modo M está ativo)
        // ----------------------------------------------------------------------
        if (materialMode && !meshes.empty())
        {
            Mesh& sel  = meshes[selectedObject]; // referência ao objeto selecionado

            // step = variação por frame, proporcional ao tempo.
            // 0.5 unidades/segundo. Com deltaTime ≈ 0.016 → step ≈ 0.008 por frame.
            float step = 0.5f * deltaTime;

            // Verifica se + ou - está pressionado (teclado numérico ou alfanumérico)
            bool up   = (glfwGetKey(window, GLFW_KEY_KP_ADD)      == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_EQUAL)        == GLFW_PRESS);
            bool down = (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_MINUS)        == GLFW_PRESS);

            if (up || down)
            {
                float dir = up ? 1.0f : -1.0f; // +1 aumenta, -1 diminui

                // switch determina qual coeficiente ajustar com base em editComponent
                switch (editComponent)
                {
                    // glm::clamp mantém o valor dentro dos limites válidos [min, max]
                    case 0: sel.ka = glm::clamp(sel.ka + dir * step, 0.0f, 1.0f); break;
                    case 1: sel.kd = glm::clamp(sel.kd + dir * step, 0.0f, 1.0f); break;
                    case 2: sel.ks = glm::clamp(sel.ks + dir * step, 0.0f, 1.0f); break;
                    // shininess tem range maior [1, 256], e cresce mais rápido (×25)
                    case 3: sel.shininess = glm::clamp(sel.shininess + dir * 25.0f * deltaTime,
                                                       1.0f, 256.0f); break;
                }

                // Throttle: imprime no console no máximo 4 vezes por segundo.
                // static persiste entre chamadas do loop.
                static float lastPrint = 0.0f;
                if (now - lastPrint > 0.25f) { printMaterial(selectedObject); lastPrint = now; }
            }
        }

        // ----------------------------------------------------------------------
        // TRANSLAÇÃO CONTÍNUA DO OBJETO SELECIONADO
        // ----------------------------------------------------------------------
        if (!meshes.empty())
        {
            Mesh& sel = meshes[selectedObject];
            float ts  = 2.0f * deltaTime; // 2 unidades/segundo

            // Setas: eixos X e Y
            if (glfwGetKey(window, GLFW_KEY_LEFT)      == GLFW_PRESS) sel.position.x -= ts;
            if (glfwGetKey(window, GLFW_KEY_RIGHT)     == GLFW_PRESS) sel.position.x += ts;
            if (glfwGetKey(window, GLFW_KEY_UP)        == GLFW_PRESS) sel.position.y += ts;
            if (glfwGetKey(window, GLFW_KEY_DOWN)      == GLFW_PRESS) sel.position.y -= ts;
            // Page Up/Down: eixo Z
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) sel.position.z += ts;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) sel.position.z -= ts;
        }

        // ----------------------------------------------------------------------
        // LIMPA OS BUFFERS (início do frame)
        // ----------------------------------------------------------------------
        // Define a cor de fundo (cinza escuro: R=0.12, G=0.12, B=0.15, A=1.0)
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

        // Limpa DOIS buffers:
        //   GL_COLOR_BUFFER_BIT: apaga os pixels do frame anterior
        //   GL_DEPTH_BUFFER_BIT: reseta o Z-buffer (profundidade de cada pixel)
        //                        CRÍTICO: sem isso, o Z-buffer do frame anterior
        //                        impediria objetos de serem desenhados corretamente
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ----------------------------------------------------------------------
        // MATRIZES VIEW E PROJECTION — enviadas à GPU como uniforms
        // ----------------------------------------------------------------------

        // Recalcula a View Matrix com a posição/direção atual da câmera.
        glm::mat4 view = camera.getViewMatrix();
        // Envia para o vertex shader: "view" uniform
        // GL_FALSE = não transpor a matriz (GLM já usa a convenção do OpenGL)
        glUniformMatrix4fv(glGetUniformLocation(shaderID, "view"), 1, GL_FALSE, glm::value_ptr(view));

        // Envia a posição da câmera para o FRAGMENT shader (necessário para componente especular)
        glUniform3fv(glGetUniformLocation(shaderID, "viewPos"), 1, glm::value_ptr(camera.position));

        // Calcula a Projection Matrix com base no modo atual
        glm::mat4 proj = usePerspective
            // Perspectiva: FOV=45°, aspect ratio da janela, near=0.1, far=100
            // Near/far = planos de corte. Objetos fora desse intervalo não aparecem.
            ? glm::perspective(glm::radians(45.0f), (float)WIDTH / HEIGHT, 0.1f, 100.0f)
            // Ortográfica: sem efeito de perspectiva. Útil para vistas técnicas.
            : glm::ortho(-5.0f, 5.0f, -5.0f * (float)HEIGHT / WIDTH, 5.0f * (float)HEIGHT / WIDTH,
                         0.1f, 100.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderID, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

        // ----------------------------------------------------------------------
        // LOOP DE DESENHO — um por objeto
        // ----------------------------------------------------------------------
        for (int i = 0; i < (int)meshes.size(); i++)
        {
            // Pula objetos que falharam ao carregar (VAO=0 = não inicializado)
            if (meshes[i].VAO == 0) continue;

            // Calcula a Matriz Model com posição/rotação/escala atuais deste objeto
            glm::mat4 model = meshes[i].getModelMatrix();

            // Calcula a Normal Matrix: transpose(inverse(mat3(model)))
            // Por quê? Se model tiver escala não-uniforme, normais ficariam distorcidas.
            // Ex: scale(2,1,1) tornaria um cubo mais largo, mas as normais não podem
            // simplesmente dobrar em X — isso quebraria o cálculo de iluminação.
            glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));

            // Envia as matrizes para o vertex shader
            glUniformMatrix4fv(glGetUniformLocation(shaderID, "model"),        1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(glGetUniformLocation(shaderID, "normalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMat));
            // Nota: glUniformMatrix3fv para mat3 (3×3), não 4×4

            // Destaque sutil no objeto selecionado: mistura a cor com amarelo (12%)
            // glm::mix(a, b, t) = interpolação linear: a*(1-t) + b*t
            glm::vec3 color = meshes[i].color;
            if (i == selectedObject)
                color = glm::mix(color, glm::vec3(1.0f, 1.0f, 0.3f), 0.12f);

            // Envia propriedades de material do OBJETO ATUAL para o fragment shader
            glUniform3fv(glGetUniformLocation(shaderID, "objectColor"), 1, glm::value_ptr(color));
            glUniform3fv(glGetUniformLocation(shaderID, "ka"),          1, glm::value_ptr(meshes[i].ka));
            glUniform3fv(glGetUniformLocation(shaderID, "kd"),          1, glm::value_ptr(meshes[i].kd));
            glUniform3fv(glGetUniformLocation(shaderID, "ks"),          1, glm::value_ptr(meshes[i].ks));
            glUniform1f (glGetUniformLocation(shaderID, "shininess"),   meshes[i].shininess);
            // glUniform1f = envia 1 float escalar (não vetor)

            // Ativa o VAO deste objeto: diz à GPU qual geometria usar
            glBindVertexArray(meshes[i].VAO);

            // ---- Desenho SÓLIDO com iluminação Phong ----
            // GL_FILL = preenchimento completo dos triângulos
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            // DRAWCALL: dispara o pipeline gráfico na GPU.
            //   GL_TRIANGLES = grupos de 3 vértices formam triângulos
            //   0            = começa no vértice 0
            //   nVertices    = quantos vértices processar
            glDrawArrays(GL_TRIANGLES, 0, meshes[i].nVertices);

            // ---- Desenho WIREFRAME sobreposto (se ativado com tecla B) ----
            if (wireframe)
            {
                // Sobrescreve os uniforms de material com valores "flat" (sem iluminação)
                // para que o wireframe apareça sempre na mesma cor, independente da luz
                glm::vec3 wc(0.85f); // cinza claro
                glUniform3fv(glGetUniformLocation(shaderID, "objectColor"), 1, glm::value_ptr(wc));
                glUniform3fv(glGetUniformLocation(shaderID, "ka"), 1, glm::value_ptr(glm::vec3(1.0f)));
                glUniform3fv(glGetUniformLocation(shaderID, "kd"), 1, glm::value_ptr(glm::vec3(0.0f)));
                glUniform3fv(glGetUniformLocation(shaderID, "ks"), 1, glm::value_ptr(glm::vec3(0.0f)));

                // PolygonOffset evita "z-fighting": quando dois polígonos exatamente
                // sobrepostos competem pelo Z-buffer e causam cintilação.
                // (-1, -1) desloca o wireframe levemente "para frente" (menor profundidade).
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1.0f, -1.0f);

                // Muda para modo de linhas: só as arestas dos triângulos são desenhadas
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

                // Segundo drawcall com o MESMO VAO, desta vez em modo linha.
                // Dois drawcalls = sólido + wireframe sobrepostos.
                glDrawArrays(GL_TRIANGLES, 0, meshes[i].nVertices);

                glDisable(GL_POLYGON_OFFSET_LINE);
            }

            // Desvincula o VAO — boa prática para evitar modificações acidentais
            glBindVertexArray(0);
        }

        // Restaura o modo de preenchimento para o próximo frame
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // DOUBLE BUFFERING:
        // O programa renderiza em um buffer "traseiro" (back buffer) enquanto
        // o buffer "frontal" (front buffer) está sendo exibido na tela.
        // SwapBuffers troca os dois: o frame recém-renderizado vai para a tela.
        // Evita flickering (tela piscando durante a renderização).
        glfwSwapBuffers(window);

    } // fim do game loop

    // -------------------------------------------------------------------------
    // LIMPEZA DE RECURSOS
    // -------------------------------------------------------------------------
    // Boa prática: liberar recursos alocados antes de encerrar.
    for (auto& m : meshes)
        if (m.VAO) glDeleteVertexArrays(1, &m.VAO); // libera VAO da GPU
    glDeleteProgram(shaderID); // libera o programa de shader da GPU
    glfwTerminate();           // libera todos os recursos do GLFW
    return 0;
}

// =============================================================================
// printMaterial() — Imprime estado atual do material no console
// =============================================================================
void printMaterial(int idx)
{
    // Verificação de bounds: idx deve ser um índice válido do vetor meshes
    if (idx < 0 || idx >= (int)meshes.size()) return;

    const Mesh& m = meshes[idx]; // referência const: não modifica o mesh

    // Nomes dos componentes para o feedback no console
    const char* comp[] = { "ka (ambiente)", "kd (difuso)", "ks (especular)", "shininess" };

    // fixed + setprecision(3) = sempre 3 casas decimais (ex: 0.800 em vez de 0.8)
    cout << fixed << setprecision(3);
    cout << "--- Obj[" << idx << "]"
         << " | ka=(" << m.ka.r << "," << m.ka.g << "," << m.ka.b << ")"
         << " kd=(" << m.kd.r << "," << m.kd.g << "," << m.kd.b << ")"
         << " ks=(" << m.ks.r << "," << m.ks.g << "," << m.ks.b << ")"
         << " shi=" << m.shininess
         << " | editando: [" << comp[editComponent] << "]\n";
}

// =============================================================================
// key_callback() — Chamado pelo GLFW a cada evento de teclado
// =============================================================================
// DIFERENÇA DE glfwGetKey NO LOOP:
//   key_callback  → chamado UMA VEZ por evento (pressionar ou soltar)
//   glfwGetKey    → verificado CONTINUAMENTE a cada frame
// Aqui tratamos ações discretas (toggle, seleção). No loop tratamos movimento.
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    // Fecha a janela quando ESC é pressionado
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);

    // Ignora GLFW_RELEASE e (para a maioria das teclas) GLFW_REPEAT.
    // REPEAT seria o comportamento de "segurar" — tratado no loop via glfwGetKey.
    if (action != GLFW_PRESS) return;
    if (meshes.empty()) return;

    // Referência ao objeto atualmente selecionado.
    // '&' = sem cópia: modificar sel modifica meshes[selectedObject] diretamente.
    Mesh& sel = meshes[selectedObject];

    // ---- TAB: cicla pelos objetos ----
    if (key == GLFW_KEY_TAB)
    {
        // Módulo (%) garante que o índice fique em [0, meshes.size()-1].
        // Ex: com 3 objetos: 0→1→2→0→1→...
        selectedObject = (selectedObject + 1) % (int)meshes.size();
        cout << "\n>> Selecionado: objeto " << selectedObject << "\n";
        printMaterial(selectedObject);
        return; // evita processar as próximas condições
    }

    // ---- M: toggle do modo de edição de material ----
    if (key == GLFW_KEY_M)
    {
        // '!' inverte o booleano: true→false, false→true
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

    // ---- Dentro do modo material: teclas 1-4 selecionam o componente ----
    if (materialMode)
    {
        if (key == GLFW_KEY_1) { editComponent = 0; cout << "[editando: ka]\n"; }
        if (key == GLFW_KEY_2) { editComponent = 1; cout << "[editando: kd]\n"; }
        if (key == GLFW_KEY_3) { editComponent = 2; cout << "[editando: ks]\n"; }
        if (key == GLFW_KEY_4) { editComponent = 3; cout << "[editando: shininess]\n"; }
        // O ajuste +/- é feito no game loop (contínuo via glfwGetKey).
        // return aqui bloqueia X/Y/Z/R/F enquanto o modo M está ativo.
        return;
    }

    // ---- Rotação do objeto selecionado ----
    const float rStep = 5.0f; // 5 graus por pressionada
    if (key == GLFW_KEY_X) sel.rotation.x += rStep; // incrementa rotação X em graus
    if (key == GLFW_KEY_Y) sel.rotation.y += rStep; // Y
    if (key == GLFW_KEY_Z) sel.rotation.z += rStep; // Z
    // A rotação é aplicada em getModelMatrix() na próxima vez que o frame for desenhado.

    // ---- Escala uniforme (todos os eixos juntos) ----
    const float sStep = 0.05f;
    if (key == GLFW_KEY_R) sel.scale += glm::vec3(sStep); // aumenta 5%

    // Diminui 5% mas garante escala mínima de 0.05 (evita escala zero ou negativa)
    if (key == GLFW_KEY_F) sel.scale = glm::max(sel.scale - glm::vec3(sStep), glm::vec3(0.05f));

    // ---- Toggle de projeção ----
    if (key == GLFW_KEY_O)
    {
        usePerspective = !usePerspective;
        cout << "[Projecao: " << (usePerspective ? "Perspectiva" : "Ortografica") << "]\n";
    }

    // ---- Toggle de wireframe ----
    if (key == GLFW_KEY_B)
    {
        wireframe = !wireframe;
        cout << "[Wireframe: " << (wireframe ? "ON" : "OFF") << "]\n";
    }

    // ---- Reset de transformações ----
    if (key == GLFW_KEY_BACKSPACE)
    {
        sel.rotation = glm::vec3(0.0f); // sem rotação
        sel.scale    = glm::vec3(1.0f); // escala original
        cout << "[Objeto " << selectedObject << " resetado]\n";
    }
}

// =============================================================================
// mouse_callback() — Chamado pelo GLFW quando o mouse se move
// =============================================================================
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    // No primeiro frame, inicializa lastX/Y com a posição atual do mouse.
    // Sem isso, haveria um offset gigante no primeiro movimento (pulo de câmera).
    if (firstMouse)
    {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    // Calcula o quanto o mouse se moveu desde o último frame
    float xoff =  (float)xpos - lastX; // movimento horizontal
    float yoff =  lastY - (float)ypos; // INVERTIDO: tela Y cresce para baixo,
                                       // mas pitch positivo = olhar para cima

    // Atualiza a posição anterior para o próximo frame
    lastX = (float)xpos;
    lastY = (float)ypos;

    // Envia os offsets para a câmera (que aplica sensibilidade e atualiza yaw/pitch)
    camera.processMouseMovement(xoff, yoff);
}

// =============================================================================
// scroll_callback() — Chamado pelo GLFW quando o scroll do mouse é usado
// =============================================================================
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Ajusta a velocidade de movimento da câmera com o scroll.
    // yoffset: +1 = scroll para cima (aumenta velocidade), -1 = para baixo (diminui).
    // glm::clamp: mantém a velocidade entre 1.0 e 20.0.
    camera.movementSpeed = glm::clamp(
        camera.movementSpeed + (float)yoffset * 0.5f,
        1.0f,   // mínimo: câmera não para completamente
        20.0f   // máximo: câmera não fica insanamente rápida
    );
}

// =============================================================================
// setupShaders() — Compila os shaders GLSL e cria o programa na GPU
// =============================================================================
// Retorna o ID do programa de shader (usado em glUseProgram e glGetUniformLocation).
GLuint setupShaders()
{
    // Lambda (função anônima) que compila um shader de qualquer tipo.
    // Parâmetros: tipo (GL_VERTEX_SHADER ou GL_FRAGMENT_SHADER) e código fonte GLSL.
    // Retorna o ID do shader compilado.
    auto compile = [](GLenum type, const GLchar* src) -> GLuint
    {
        // Cria um objeto de shader na GPU
        GLuint s = glCreateShader(type);

        // Associa o código fonte ao objeto de shader.
        // "1" = número de strings (passamos uma string só).
        // nullptr = comprimentos (nullptr = strings terminadas em \0).
        glShaderSource(s, 1, &src, nullptr);

        // Compila o GLSL em código de máquina da GPU (como um compilador C++)
        glCompileShader(s);

        // Verifica erros de compilação
        GLint ok;
        GLchar log[512];
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glGetShaderInfoLog(s, 512, nullptr, log);
            cerr << "Shader compile error:\n" << log << "\n";
        }
        return s;
    };

    // Compila os dois shaders
    GLuint vs = compile(GL_VERTEX_SHADER,   vertexShaderSource);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSource);

    // Cria o programa de shader: contêiner que agrupa vertex + fragment shader
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); // anexa o vertex shader
    glAttachShader(p, fs); // anexa o fragment shader

    // Linka o programa: verifica que as saídas do VS batem com as entradas do FS
    // e gera o executável final que rodará na GPU.
    glLinkProgram(p);

    // Verifica erros de linkagem
    GLint ok;
    GLchar log[512];
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(p, 512, nullptr, log);
        cerr << "Shader link error:\n" << log << "\n";
    }

    // Após linkar, os objetos de shader individuais não são mais necessários.
    // Libera a memória deles (o programa já contém o código compilado).
    glDeleteShader(vs);
    glDeleteShader(fs);

    return p; // retorna o ID do programa — usado em glUseProgram()
}