#ifndef LOADOBJ_H
#define LOADOBJ_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Mesh.h"

// Carrega um arquivo .obj e preenche um Mesh com VAO/VBO contendo:
//   layout 0: posição   (x, y, z)      -- 3 floats
//   layout 1: normal    (nx, ny, nz)   -- 3 floats
//   layout 2: texCoord  (s, t)         -- 2 floats
// Total por vértice: 8 floats, stride = 8 * sizeof(GLfloat)
bool loadSimpleOBJ(const std::string& filePath, Mesh& outMesh)
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;
    std::vector<GLfloat>   vBuffer;

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[OBJ] Erro ao abrir: " << filePath << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v")
        {
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (token == "vt")
        {
            glm::vec2 vt;
            ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        }
        else if (token == "vn")
        {
            glm::vec3 vn;
            ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (token == "f")
        {
            // Suporte a faces triangulares: v/vt/vn
            std::string faceToken;
            std::vector<std::string> faceVerts;
            while (ss >> faceToken)
                faceVerts.push_back(faceToken);

            // Fan triangulation para faces com 4+ vértices
            for (int i = 1; i + 1 < (int)faceVerts.size(); i++)
            {
                std::string trio[3] = { faceVerts[0], faceVerts[i], faceVerts[i + 1] };
                for (const auto& word : trio)
                {
                    int vi = 0, ti = 0, ni = 0;
                    std::istringstream ws(word);
                    std::string idx;

                    if (std::getline(ws, idx, '/')) vi = idx.empty() ? 0 : std::stoi(idx) - 1;
                    if (std::getline(ws, idx, '/')) ti = idx.empty() ? 0 : std::stoi(idx) - 1;
                    if (std::getline(ws, idx))      ni = idx.empty() ? 0 : std::stoi(idx) - 1;

                    // Posição
                    glm::vec3 pos = (vi >= 0 && vi < (int)positions.size()) ? positions[vi] : glm::vec3(0);
                    vBuffer.push_back(pos.x);
                    vBuffer.push_back(pos.y);
                    vBuffer.push_back(pos.z);

                    // Normal
                    glm::vec3 nor = (ni >= 0 && ni < (int)normals.size()) ? normals[ni] : glm::vec3(0, 1, 0);
                    vBuffer.push_back(nor.x);
                    vBuffer.push_back(nor.y);
                    vBuffer.push_back(nor.z);

                    // Textura
                    glm::vec2 tex = (ti >= 0 && ti < (int)texCoords.size()) ? texCoords[ti] : glm::vec2(0);
                    vBuffer.push_back(tex.s);
                    vBuffer.push_back(tex.t);
                }
            }
        }
    }
    file.close();

    if (vBuffer.empty())
    {
        std::cerr << "[OBJ] Nenhuma face encontrada em: " << filePath << std::endl;
        return false;
    }

    // ---- Cria VBO e VAO ----
    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 8 * sizeof(GLfloat);

    // layout 0: posição (x,y,z)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // layout 1: normal (nx,ny,nz)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // layout 2: texCoord (s,t)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    outMesh.VAO       = VAO;
    outMesh.nVertices = (int)(vBuffer.size() / 8);
    outMesh.name      = filePath;

    std::cout << "[OBJ] Carregado: " << filePath
              << " | vertices: " << outMesh.nVertices << std::endl;
    return true;
}

#endif
