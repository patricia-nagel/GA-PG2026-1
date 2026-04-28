# Visualizador 3D com OpenGL Moderna – Grau A

Leitor e visualizador de cenas 3D implementado com OpenGL 3.3 Core Profile, GLFW, GLAD e GLM.

## Integrantes
- Cássio Braga
- Gabriel Walber
- Patrícia Nagel

---

## Dependências

| Biblioteca | Uso |
|------------|-----|
| GLFW 3.x   | Janela, contexto OpenGL e input |
| GLAD (OpenGL 3.3) | Carregamento de ponteiros de funções OpenGL |
| GLM 0.9.x  | Álgebra linear (matrizes, vetores, quaternions) |

---

## Compilação

### Com CMake (recomendado)

```bash
mkdir build && cd build
cmake ..
cmake --build .
./GrauA
```

Exemplo de `CMakeLists.txt` mínimo:

```cmake
cmake_minimum_required(VERSION 3.10)
project(GrauA)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3  REQUIRED)
find_package(glm    REQUIRED)

add_executable(GrauA
    main.cpp
    Camera.cpp
    glad/src/glad.c      # ajuste para o caminho do seu glad
)

target_include_directories(GrauA PRIVATE glad/include .)
target_link_libraries(GrauA OpenGL::GL glfw glm::glm)
```

### Compilação manual (Linux/macOS)

```bash
g++ -std=c++17 main.cpp Camera.cpp glad/src/glad.c \
    -I glad/include -I . \
    -lglfw -lGL -ldl -o visualizador
./visualizador
```

### Windows (MinGW)

```bash
g++ -std=c++17 main.cpp Camera.cpp glad/src/glad.c \
    -I glad/include -I . \
    -lglfw3 -lopengl32 -lgdi32 -o visualizador.exe
visualizador.exe
```

---

## Configurando os Modelos

Edite o vetor `configs` em `main.cpp`:

```cpp
vector<ObjCfg> configs = {
    { "caminho/para/objeto1.obj", posição, cor, ka, kd, ks, shininess },
    { "caminho/para/objeto2.obj", ...                                  },
    { "caminho/para/objeto3.obj", ...                                  },
};
```

Os modelos precisam ter normais (`vn`) e coordenadas de textura (`vt`) definidas no arquivo `.obj`.

---

## Controles Completos

### Câmera (FPS)

| Tecla / Ação | Efeito |
|---|---|
| W / S | Avança / Recua |
| A / D | Desloca esquerda / direita |
| Q / E | Desce / Sobe |
| Mouse | Rotaciona câmera (yaw + pitch) |
| Scroll | Ajusta velocidade de movimento |

### Seleção e Transformações do Objeto

| Tecla | Efeito |
|---|---|
| **TAB** | Alterna objeto selecionado (0 → 1 → 2 → ...) |
| X / Y / Z | Rotaciona o objeto selecionado no eixo X / Y / Z (+5° por pressionada) |
| ← → ↑ ↓ | Translada o objeto nos eixos X e Y (contínuo) |
| Page Up / Page Down | Translada o objeto no eixo Z (contínuo) |
| R / F | Aumenta / Diminui escala uniforme |
| Backspace | Reseta rotação e escala do objeto selecionado |

### Visualização

| Tecla | Efeito |
|---|---|
| O | Alterna Perspectiva / Ortográfica |
| B | Liga/desliga wireframe sobreposto à geometria sólida |

### Modo Material — Ajuste de Phong em Tempo Real

| Tecla | Efeito |
|---|---|
| **M** | **Entra / Sai do modo material** |
| 1 | Seleciona `ka` (coeficiente ambiente) para edição |
| 2 | Seleciona `kd` (coeficiente difuso) para edição |
| 3 | Seleciona `ks` (coeficiente especular) para edição |
| 4 | Seleciona `shininess` para edição |
| **+ (segurado)** | Aumenta o componente selecionado continuamente |
| **- (segurado)** | Diminui o componente selecionado continuamente |

> O valor atual é impresso no console a cada ~0.25s enquanto o ajuste está acontecendo.
> No modo material as teclas de transformação (X/Y/Z/R/F/setas) ficam desativadas para evitar conflitos.

---

## Arquitetura

```
main.cpp     – loop principal, callbacks, matrizes MVP, uniforms Phong, modo material
Camera.h/.cpp – câmera FPS: posição, WASD + mouse, ângulos de Euler (yaw/pitch)
Mesh.h       – struct com VAO, nVertices, posição/rotação/escala e ka/kd/ks/shininess
LoadOBJ.h    – parser .obj: lê v/vt/vn/f e monta VBO com 8 floats/vértice
```

### Layout do VBO (stride = 8 × sizeof(float))

| Location | Atributo  | Componentes | Offset |
|----------|-----------|-------------|--------|
| 0        | Posição   | x, y, z     | 0      |
| 1        | Normal    | nx, ny, nz  | 12     |
| 2        | TexCoord  | s, t        | 24     |

### Modelo de Iluminação de Phong (Fragment Shader)

```
FragColor = (ka·Iamb + kd·max(N·L,0)·Idiff + ks·max(R·V,0)^shininess·Ispec) × objectColor
```

- **N** = normal interpolada (normalizada)  
- **L** = vetor da superfície à luz  
- **R** = reflexo de L em torno de N  
- **V** = vetor da superfície à câmera  

Propriedades por objeto: `ka`, `kd`, `ks`, `shininess` e `color` — todas ajustáveis em tempo real via teclado.
