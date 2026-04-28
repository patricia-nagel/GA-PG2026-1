#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

struct Mesh
{
    GLuint VAO       = 0;
    int    nVertices = 0;
    std::string name;

    // Transformações individuais
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // graus em X, Y, Z
    glm::vec3 scale    = glm::vec3(1.0f);

    // Propriedades de material (Phong)
    glm::vec3 ka = glm::vec3(0.1f);        // coeficiente ambiente
    glm::vec3 kd = glm::vec3(0.8f);        // coeficiente difuso
    glm::vec3 ks = glm::vec3(0.5f);        // coeficiente especular
    float     shininess = 32.0f;           // brilho especular

    // Cor base (para objetos sem textura)
    glm::vec3 color = glm::vec3(1.0f, 0.5f, 0.2f);

    // Monta a matriz model para este objeto
    glm::mat4 getModelMatrix() const
    {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);
        return model;
    }
};

#endif
