// =============================================================================
// LoadOBJ.h — Parser do arquivo .obj e criação do VBO/VAO na GPU
// =============================================================================
// ESTE É O PONTO ONDE OS DADOS "CRUZAM" DA CPU PARA A GPU.
//
// FLUXO COMPLETO:
//   1. Lê o arquivo .obj (texto) linha por linha  →  CPU (RAM)
//   2. Armazena vértices, normais e texCoords em vetores temporários  →  CPU (RAM)
//   3. Para cada face, monta o vBuffer com os 8 floats por vértice  →  CPU (RAM)
//   4. glBufferData() copia o vBuffer para um VBO na VRAM (GPU)
//   5. glVertexAttribPointer() registra no VAO como ler o VBO
//   6. Retorna o VAO ID — de agora em diante, a GPU sabe tudo sozinha
// =============================================================================

#ifndef LOADOBJ_H
#define LOADOBJ_H

#include <iostream>  // cerr para mensagens de erro
#include <fstream>   // ifstream: leitura de arquivos
#include <sstream>   // istringstream: "tokenizar" uma string linha por linha
#include <string>    // std::string
#include <vector>    // std::vector: array dinâmico

#include <glad/glad.h> // GLuint, GLfloat, GLsizei e funções gl*
#include <glm/glm.hpp> // glm::vec2, glm::vec3

#include "Mesh.h"      // struct Mesh (onde salvaremos VAO e nVertices)

// -----------------------------------------------------------------------------
// loadSimpleOBJ()
// -----------------------------------------------------------------------------
// Parâmetros:
//   filePath — caminho do arquivo .obj no disco (ex: "../assets/Cube.obj")
//   outMesh  — struct Mesh passada por REFERÊNCIA (&). A função preenche
//              outMesh.VAO e outMesh.nVertices diretamente.
//
// Retorno:
//   true  — carregou com sucesso
//   false — erro ao abrir arquivo ou arquivo sem faces
// -----------------------------------------------------------------------------
bool loadSimpleOBJ(const std::string& filePath, Mesh& outMesh)
{
    // -------------------------------------------------------------------------
    // VETORES TEMPORÁRIOS NA CPU (RAM)
    // -------------------------------------------------------------------------
    // O formato .obj separa posições, normais e texCoords em listas distintas.
    // As faces referenciam essas listas por índice.
    // Precisamos dessas listas temporárias para "resolver" os índices.

    // Guarda todas as linhas "v x y z" lidas (posições dos vértices)
    std::vector<glm::vec3> positions;

    // Guarda todas as linhas "vt s t" lidas (coordenadas de textura, 0.0 a 1.0)
    std::vector<glm::vec2> texCoords;

    // Guarda todas as linhas "vn nx ny nz" lidas (vetores normais)
    std::vector<glm::vec3> normals;

    // BUFFER FINAL: dados entrelaçados, prontos para enviar à GPU.
    // Layout: [x,y,z, nx,ny,nz, s,t, | x,y,z, nx,ny,nz, s,t | ...]
    // = 8 floats por vértice.
    // Este vetor será copiado inteiramente para a VRAM via glBufferData.
    std::vector<GLfloat> vBuffer;

    // -------------------------------------------------------------------------
    // ABRE O ARQUIVO
    // -------------------------------------------------------------------------

    // ifstream = input file stream. Abre o arquivo em modo leitura.
    std::ifstream file(filePath);

    // Verifica se o arquivo foi encontrado e aberto com sucesso.
    if (!file.is_open())
    {
        // cerr = console de erro (separado do cout). Não encerra o programa.
        std::cerr << "[OBJ] Erro ao abrir: " << filePath << std::endl;
        return false; // Sinaliza falha ao chamador
    }

    // -------------------------------------------------------------------------
    // LEITURA LINHA POR LINHA
    // -------------------------------------------------------------------------

    std::string line; // Buffer para armazenar a linha atual

    // getline(file, line) lê uma linha completa do arquivo.
    // O loop termina quando chegar ao fim do arquivo (getline retorna false).
    while (std::getline(file, line))
    {
        // Cria um "stream" a partir da linha de texto.
        // Isso permite usar >> para extrair palavras/números separados por espaço.
        std::istringstream ss(line);

        // Extrai a primeira palavra da linha (o "tipo" da informação).
        // Exemplos: "v", "vt", "vn", "f", "#" (comentário), "o" (nome), "usemtl", etc.
        std::string token;
        ss >> token;

        // ---------------------------------------------------------------------
        // LINHA DE VÉRTICE: "v x y z"
        // ---------------------------------------------------------------------
        if (token == "v")
        {
            glm::vec3 v;
            // Extrai os 3 floats X, Y, Z diretamente para os campos do vec3
            ss >> v.x >> v.y >> v.z;
            // Adiciona ao vetor. ÍNDICE NO .OBJ = posição neste vetor + 1
            // (o .obj usa índices começando em 1, não em 0)
            positions.push_back(v);
        }
        // ---------------------------------------------------------------------
        // LINHA DE TEXCOORD: "vt s t"
        // ---------------------------------------------------------------------
        else if (token == "vt")
        {
            glm::vec2 vt;
            ss >> vt.s >> vt.t; // s = horizontal (U), t = vertical (V)
            texCoords.push_back(vt);
        }
        // ---------------------------------------------------------------------
        // LINHA DE NORMAL: "vn nx ny nz"
        // ---------------------------------------------------------------------
        else if (token == "vn")
        {
            glm::vec3 vn;
            ss >> vn.x >> vn.y >> vn.z;
            // As normais do .obj são unitárias (comprimento ≈ 1).
            // Representam a direção "para fora" da superfície.
            normals.push_back(vn);
        }
        // ---------------------------------------------------------------------
        // LINHA DE FACE: "f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ..."
        // ---------------------------------------------------------------------
        else if (token == "f")
        {
            // Uma face pode ter 3 (triângulo) ou 4+ (quad, polígono) vértices.
            // Coletamos todos os grupos "v/vt/vn" da linha em um vetor.
            std::string faceToken;
            std::vector<std::string> faceVerts;
            while (ss >> faceToken)
                faceVerts.push_back(faceToken); // ex: ["1/1/1", "2/2/2", "3/3/3", "4/4/4"]

            // FAN TRIANGULATION: converte qualquer polígono em triângulos.
            // Para um quad [0,1,2,3], gera dois triângulos: [0,1,2] e [0,2,3].
            // Para um triângulo [0,1,2], o loop roda apenas 1 vez (i=1).
            //
            // Padrão fan: primeiro vértice (índice 0) é sempre o "pivô",
            // os outros vão de i=1 até o penúltimo.
            for (int i = 1; i + 1 < (int)faceVerts.size(); i++)
            {
                // Os 3 grupos que formam este triângulo
                std::string trio[3] = { faceVerts[0], faceVerts[i], faceVerts[i + 1] };

                // Para cada um dos 3 vértices do triângulo
                for (const auto& word : trio)
                {
                    // Índices inicializados com 0 como fallback.
                    int vi = 0, ti = 0, ni = 0;

                    // Cria um stream a partir do token "v/vt/vn" (ex: "12/5/8")
                    std::istringstream ws(word);
                    std::string idx;

                    // getline(ws, idx, '/') lê até o próximo '/' (ou fim da string)
                    // e armazena em idx. Retorna false se não houver mais dados.

                    // Índice de posição (1ª parte antes do primeiro '/')
                    // stoi() converte string para int. Subtrai 1 para converter base-1 → base-0.
                    if (std::getline(ws, idx, '/')) vi = idx.empty() ? 0 : std::stoi(idx) - 1;

                    // Índice de texCoord (2ª parte). Pode estar vazio em formato "v//vn".
                    if (std::getline(ws, idx, '/')) ti = idx.empty() ? 0 : std::stoi(idx) - 1;

                    // Índice de normal (3ª parte, sem delimitador final)
                    if (std::getline(ws, idx))      ni = idx.empty() ? 0 : std::stoi(idx) - 1;

                    // ---------------------------------------------------------
                    // MONTA O VBUFFER — AQUI OS DADOS SÃO "ENTRELAÇADOS"
                    // ---------------------------------------------------------
                    // Para cada vértice de face, empurramos 8 floats no vBuffer:
                    // 3 de posição + 3 de normal + 2 de texCoord = 8 floats/vértice

                    // Busca a posição pelo índice (com verificação de bounds)
                    glm::vec3 pos = (vi >= 0 && vi < (int)positions.size())
                                  ? positions[vi] : glm::vec3(0.0f);
                    vBuffer.push_back(pos.x); // layout 0, offset 0
                    vBuffer.push_back(pos.y); // layout 0, offset 4
                    vBuffer.push_back(pos.z); // layout 0, offset 8 → 3 floats de posição

                    // Busca a normal pelo índice
                    // Fallback (0,1,0) = normal apontando para cima, evita normais zeradas
                    glm::vec3 nor = (ni >= 0 && ni < (int)normals.size())
                                  ? normals[ni] : glm::vec3(0.0f, 1.0f, 0.0f);
                    vBuffer.push_back(nor.x); // layout 1, offset 12
                    vBuffer.push_back(nor.y); // layout 1, offset 16
                    vBuffer.push_back(nor.z); // layout 1, offset 20 → 3 floats de normal

                    // Busca a coord de textura pelo índice
                    glm::vec2 tex = (ti >= 0 && ti < (int)texCoords.size())
                                  ? texCoords[ti] : glm::vec2(0.0f);
                    vBuffer.push_back(tex.s); // layout 2, offset 24
                    vBuffer.push_back(tex.t); // layout 2, offset 28 → 2 floats de texCoord

                    // Total: 8 floats × sizeof(GLfloat=4bytes) = 32 bytes por vértice (o stride)
                }
            }
        }
        // Linhas com outros tokens (#, o, g, mtllib, usemtl, s) são ignoradas.
    }

    // Fecha o arquivo — já temos todos os dados nos vetores temporários
    file.close();

    // Verificação de segurança: se não havia faces no arquivo, nada foi adicionado ao vBuffer
    if (vBuffer.empty())
    {
        std::cerr << "[OBJ] Nenhuma face encontrada em: " << filePath << std::endl;
        return false;
    }

    // =========================================================================
    // UPLOAD PARA A GPU — AQUI OS DADOS CRUZAM DA RAM PARA A VRAM
    // =========================================================================

    // Declara os identificadores (IDs) do VBO e do VAO.
    // São apenas inteiros; o OpenGL preenche os valores reais nas funções abaixo.
    GLuint VBO, VAO;

    // -------------------------------------------------------------------------
    // CRIAÇÃO DO VBO (Vertex Buffer Object)
    // -------------------------------------------------------------------------
    // O VBO é um bloco de memória na VRAM que guarda os dados brutos dos vértices.

    // Pede ao OpenGL para reservar 1 buffer na GPU e armazenar o ID em VBO.
    glGenBuffers(1, &VBO);

    // Ativa o VBO: qualquer operação de buffer que vier a seguir afeta ESTE buffer.
    // GL_ARRAY_BUFFER = tipo "buffer de atributos de vértice"
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    // CÓPIA: CPU → GPU
    // Envia o conteúdo de vBuffer (na RAM) para a VRAM.
    //   GL_ARRAY_BUFFER        — tipo do buffer
    //   vBuffer.size()*4       — tamanho em bytes (cada GLfloat = 4 bytes)
    //   vBuffer.data()         — ponteiro para o início do array na RAM
    //   GL_STATIC_DRAW         — hint: dados não mudarão depois de enviados
    //                            (permite que a GPU os coloque em memória rápida)
    glBufferData(GL_ARRAY_BUFFER,
                 vBuffer.size() * sizeof(GLfloat),
                 vBuffer.data(),
                 GL_STATIC_DRAW);
    // APÓS ESTA LINHA: os dados estão na GPU. O vBuffer na CPU já pode ser destruído.

    // -------------------------------------------------------------------------
    // CRIAÇÃO DO VAO (Vertex Array Object)
    // -------------------------------------------------------------------------
    // O VAO não guarda dados — guarda a "receita" de como ler o VBO.
    // É ele que o game loop usa: glBindVertexArray(VAO) antes de desenhar.

    // Cria 1 VAO e armazena o ID.
    glGenVertexArrays(1, &VAO);

    // Ativa o VAO. Toda configuração de glVertexAttribPointer feita agora
    // fica "gravada" dentro deste VAO.
    glBindVertexArray(VAO);

    // stride = tamanho em bytes de UM vértice completo = 8 floats × 4 bytes = 32 bytes.
    // É a distância entre o começo de um vértice e o começo do próximo.
    const GLsizei stride = 8 * sizeof(GLfloat);

    // ---- Atributo 0: Posição (x, y, z) ----
    // glVertexAttribPointer diz ao VAO como encontrar este atributo dentro do VBO.
    //   0          — location no vertex shader: layout(location = 0)
    //   3          — 3 componentes (x, y, z)
    //   GL_FLOAT   — tipo de cada componente
    //   GL_FALSE   — não normalizar (os valores já estão em float)
    //   stride     — 32 bytes entre vértices
    //   (GLvoid*)0 — offset 0: a posição começa no byte 0 de cada vértice
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0); // Ativa o atributo 0 (desativado por padrão)

    // ---- Atributo 1: Normal (nx, ny, nz) ----
    //   (GLvoid*)(3*4) = offset 12 bytes (vem depois dos 3 floats de posição)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // ---- Atributo 2: TexCoord (s, t) ----
    //   (GLvoid*)(6*4) = offset 24 bytes (vem depois de pos + normal = 6 floats)
    //   Só 2 componentes (s, t), não 3.
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // Desvincula o VBO e o VAO.
    // Boa prática: evita modificar acidentalmente esses objetos em código posterior.
    // O VAO "lembrou" tudo — pode ser desvinculado com segurança.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // -------------------------------------------------------------------------
    // PREENCHE A STRUCT MESH
    // -------------------------------------------------------------------------

    // Salva o ID do VAO na struct do chamador (por referência)
    outMesh.VAO       = VAO;

    // Calcula o número de vértices: total de floats ÷ 8 floats por vértice.
    // IMPORTANTE: o 8 aqui deve ser IGUAL ao stride / sizeof(GLfloat) acima.
    outMesh.nVertices = (int)(vBuffer.size() / 8);

    // Guarda o caminho para debug
    outMesh.name      = filePath;

    std::cout << "[OBJ] Carregado: " << filePath
              << " | vertices: " << outMesh.nVertices << std::endl;
    return true;
}

#endif // LOADOBJ_H