
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// STREET RACER 2.0
// C++17 + GLFW + OpenGL 3.3 Core
// ============================================================

constexpr int WINDOW_WIDTH = 1000;
constexpr int WINDOW_HEIGHT = 750;

constexpr float ROAD_LEFT = -0.90f;
constexpr float ROAD_RIGHT = 0.90f;
constexpr float MEDIAN_LEFT = -0.055f;
constexpr float MEDIAN_RIGHT = 0.055f;

constexpr float CAR_WIDTH = 0.145f;
constexpr float CAR_HEIGHT = 0.235f;
constexpr float PLAYER_Y = -0.61f;

const std::array<float, 3> PLAYER_LANES = {
    -0.755f, -0.480f, -0.205f
};

const std::array<float, 3> OPPOSITE_LANES = {
     0.205f,  0.480f,  0.755f
};

GLFWwindow* window = nullptr;

GLuint shaderProgram = 0;
GLuint vao = 0;
GLuint vbo = 0;

struct Vertex {
    float x, y;
    float r, g, b;
};

struct Car {
    float x, y;
    float r, g, b;
    bool passed = false;
};

enum class GameState {
    MENU,
    PLAYING,
    PAUSED,
    GAME_OVER
};

GameState state = GameState::MENU;

std::vector<Vertex> vertices;
std::vector<Car> traffic;
std::vector<Car> oppositeTraffic;

int playerLane = 1;
float playerX = PLAYER_LANES[1];

float roadOffset = 0.0f;
float sceneryOffset = 0.0f;

float trafficTimer = 0.0f;
float oppositeTimer = 0.0f;
float gameTime = 0.0f;
float score = 0.0f;
float bestScore = 0.0f;

bool leftWasDown = false;
bool rightWasDown = false;
bool spaceWasDown = false;
bool enterWasDown = false;
bool rWasDown = false;
bool pWasDown = false;

const char* vertexShaderSource = R"(
#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vertexColor;

void main() {
    vertexColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core

in vec3 vertexColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vertexColor, 1.0);
}
)";

// ============================================================
// SHADERS
// ============================================================

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (success != GL_TRUE) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compilation error:\n" << log << '\n';
    }

    return shader;
}

bool initGraphics() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLint vsOK = GL_FALSE;
    GLint fsOK = GL_FALSE;

    glGetShaderiv(vs, GL_COMPILE_STATUS, &vsOK);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &fsOK);

    if (vsOK != GL_TRUE || fsOK != GL_TRUE) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    shaderProgram = glCreateProgram();

    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    GLint linked = GL_FALSE;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);

    if (linked != GL_TRUE) {
        char log[1024] = {};
        glGetProgramInfoLog(
            shaderProgram, sizeof(log), nullptr, log
        );
        std::cerr << "Shader linking error:\n" << log << '\n';
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (linked != GL_TRUE) return false;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, x))
    );

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, r))
    );

    glBindVertexArray(0);
    return true;
}

// ============================================================
// DRAWING PRIMITIVES
// ============================================================

void triangle(
    float x1, float y1,
    float x2, float y2,
    float x3, float y3,
    float r, float g, float b
) {
    vertices.push_back({x1, y1, r, g, b});
    vertices.push_back({x2, y2, r, g, b});
    vertices.push_back({x3, y3, r, g, b});
}

void rect(
    float x, float y, float w, float h,
    float r, float g, float b
) {
    triangle(
        x, y, x + w, y, x + w, y + h,
        r, g, b
    );

    triangle(
        x, y, x + w, y + h, x, y + h,
        r, g, b
    );
}

void circle(
    float cx, float cy, float radius,
    float r, float g, float b
) {
    constexpr int segments = 12;

    for (int i = 0; i < segments; ++i) {
        float a1 = 6.2831853f * i / segments;
        float a2 = 6.2831853f * (i + 1) / segments;

        triangle(
            cx, cy,
            cx + std::cos(a1) * radius,
            cy + std::sin(a1) * radius,
            cx + std::cos(a2) * radius,
            cy + std::sin(a2) * radius,
            r, g, b
        );
    }
}

// ============================================================
// SIMPLE 5x7 PIXEL FONT
// ============================================================

std::array<unsigned char, 7> glyph(char c) {
    switch (static_cast<char>(std::toupper(
        static_cast<unsigned char>(c)))) {

        case 'A': return {14,17,17,31,17,17,17};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {31,4,4,4,4,4,31};
        case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17};
        case 'N': return {17,25,25,21,19,19,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};

        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};

        case ':': return {0,4,4,0,4,4,0};
        case '.': return {0,0,0,0,0,12,12};
        case '-': return {0,0,0,31,0,0,0};
        case '/': return {1,2,2,4,8,8,16};
        default:  return {0,0,0,0,0,0,0};
    }
}

void drawText(
    const std::string& text,
    float x, float topY,
    float pixel,
    float r, float g, float b
) {
    float cursor = x;

    for (char c : text) {
        auto rows = glyph(c);

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (rows[row] & (1 << (4 - col))) {
                    rect(
                        cursor + col * pixel,
                        topY - (row + 1) * pixel,
                        pixel * 0.88f,
                        pixel * 0.88f,
                        r, g, b
                    );
                }
            }
        }

        cursor += pixel * 6.0f;
    }
}

void drawCenteredText(
    const std::string& text,
    float centerX, float topY,
    float pixel,
    float r, float g, float b
) {
    float width = text.size() * 6.0f * pixel;

    drawText(
        text, centerX - width * 0.5f, topY,
        pixel, r, g, b
    );
}

// ============================================================
// CARS
// ============================================================

void drawCar(
    float x, float y,
    float r, float g, float b,
    bool facesUp,
    bool isPlayer
) {
    const float w = CAR_WIDTH;
    const float h = CAR_HEIGHT;

    // Soft shadow
    rect(
        x - w * 0.58f, y - h * 0.55f,
        w * 1.16f, h * 1.12f,
        0.035f, 0.045f, 0.045f
    );

    // Wheels
    float wheelW = w * 0.18f;
    float wheelH = h * 0.27f;

    for (int side : {-1, 1}) {
        float wx = x + side * w * 0.48f - wheelW * 0.5f;

        rect(
            wx, y - h * 0.34f,
            wheelW, wheelH,
            0.035f, 0.04f, 0.045f
        );

        rect(
            wx, y + h * 0.15f,
            wheelW, wheelH,
            0.035f, 0.04f, 0.045f
        );
    }

    // Main body
    rect(
        x - w * 0.46f, y - h * 0.50f,
        w * 0.92f, h,
        r * 0.78f, g * 0.78f, b * 0.78f
    );

    // Hood
    float frontY = facesUp ? y + h * 0.50f : y - h * 0.50f;

    rect(
        x - w * 0.36f,
        facesUp ? y + h * 0.22f : y - h * 0.32f,
        w * 0.72f, h * 0.20f,
        r, g, b
    );

    // Cabin and windows
    rect(
        x - w * 0.32f,
        y - h * 0.18f,
        w * 0.64f, h * 0.43f,
        0.10f, 0.17f, 0.22f
    );

    rect(
        x - w * 0.27f,
        y - h * 0.12f,
        w * 0.54f, h * 0.14f,
        0.29f, 0.52f, 0.62f
    );

    // A central roof highlight
    rect(
        x - w * 0.20f,
        y + h * 0.04f,
        w * 0.40f, h * 0.035f,
        0.50f, 0.64f, 0.70f
    );

    // Bumpers
    rect(
        x - w * 0.36f,
        facesUp ? y - h * 0.48f : y + h * 0.43f,
        w * 0.72f, h * 0.055f,
        0.12f, 0.13f, 0.14f
    );

    // Lights
    float lightY = facesUp ? y + h * 0.40f : y - h * 0.45f;
    float lightColorR = facesUp ? 1.0f : 1.0f;
    float lightColorG = facesUp ? 0.92f : 0.10f;
    float lightColorB = facesUp ? 0.60f : 0.08f;

    rect(
        x - w * 0.38f, lightY,
        w * 0.22f, h * 0.055f,
        lightColorR, lightColorG, lightColorB
    );

    rect(
        x + w * 0.16f, lightY,
        w * 0.22f, h * 0.055f,
        lightColorR, lightColorG, lightColorB
    );

    if (isPlayer) {
        // Blue accent on the player's car
        rect(
            x - w * 0.40f, y - h * 0.04f,
            w * 0.06f, h * 0.23f,
            0.25f, 0.90f, 1.0f
        );

        rect(
            x + w * 0.34f, y - h * 0.04f,
            w * 0.06f, h * 0.23f,
            0.25f, 0.90f, 1.0f
        );
    }

    (void)frontY;
}

// ============================================================
// ROAD AND SCENERY
// ============================================================

void drawTree(float x, float y) {
    // Shadow
    circle(x + 0.012f, y - 0.012f, 0.047f,
           0.035f, 0.12f, 0.075f);

    // Trunk
    rect(x - 0.010f, y - 0.045f,
         0.020f, 0.07f,
         0.34f, 0.20f, 0.10f);

    // Leaves
    circle(x, y, 0.045f, 0.08f, 0.40f, 0.20f);
    circle(x - 0.018f, y + 0.012f, 0.029f,
           0.13f, 0.53f, 0.25f);
}

void drawRoad() {
    // Grass
    rect(-1.0f, -1.0f, 2.0f, 2.0f,
         0.055f, 0.22f, 0.13f);

    // Decorative grass stripes
    for (int i = 0; i < 12; ++i) {
        float y = -1.0f + i * 0.20f + sceneryOffset;

        rect(-1.0f, y, 0.08f, 0.012f,
             0.08f, 0.28f, 0.16f);

        rect(0.92f, y, 0.08f, 0.012f,
             0.08f, 0.28f, 0.16f);
    }

    // Moving roadside trees
    for (int i = 0; i < 7; ++i) {
        float y = -1.05f + i * 0.34f + sceneryOffset;

        drawTree(-0.955f, y);
        drawTree(0.955f, y + 0.14f);
    }

    // Road surface
    rect(ROAD_LEFT, -1.0f,
         ROAD_RIGHT - ROAD_LEFT, 2.0f,
         0.12f, 0.13f, 0.15f);

    // Slightly different asphalt panels
    for (int i = 0; i < 8; ++i) {
        float y = -1.0f + i * 0.30f + roadOffset;

        rect(-0.87f, y, 0.78f, 0.003f,
             0.15f, 0.16f, 0.18f);

        rect(0.09f, y, 0.78f, 0.003f,
             0.15f, 0.16f, 0.18f);
    }

    // Road edges
    rect(-0.91f, -1.0f, 0.025f, 2.0f,
         0.95f, 0.91f, 0.72f);

    rect(0.885f, -1.0f, 0.025f, 2.0f,
         0.95f, 0.91f, 0.72f);

    // Dashed lane separators on both carriageways
    for (int i = 0; i < 8; ++i) {
        float y = -1.10f + i * 0.32f + roadOffset;

        // Player's side
        rect(-0.618f, y, 0.009f, 0.15f,
             0.88f, 0.88f, 0.80f);

        rect(-0.343f, y, 0.009f, 0.15f,
             0.88f, 0.88f, 0.80f);

        // Opposite side
        rect(0.334f, y, 0.009f, 0.15f,
             0.88f, 0.88f, 0.80f);

        rect(0.609f, y, 0.009f, 0.15f,
             0.88f, 0.88f, 0.80f);
    }

    // Center median
    rect(MEDIAN_LEFT, -1.0f,
         MEDIAN_RIGHT - MEDIAN_LEFT, 2.0f,
         0.27f, 0.28f, 0.28f);

    rect(-0.015f, -1.0f, 0.030f, 2.0f,
         0.90f, 0.70f, 0.20f);

    // Reflective median markers
    for (int i = 0; i < 8; ++i) {
        float y = -1.10f + i * 0.32f + roadOffset;

        rect(-0.047f, y, 0.018f, 0.055f,
             1.0f, 0.88f, 0.38f);

        rect(0.029f, y, 0.018f, 0.055f,
             1.0f, 0.88f, 0.38f);
    }

    // Direction arrows for the opposite carriageway
    for (int i = 0; i < 4; ++i) {
        float y = -1.0f + i * 0.58f + roadOffset;

        triangle(
            0.48f, y + 0.06f,
            0.45f, y + 0.015f,
            0.51f, y + 0.015f,
            0.32f, 0.34f, 0.35f
        );

        rect(0.473f, y - 0.045f,
             0.014f, 0.07f,
             0.32f, 0.34f, 0.35f);
    }
}

// ============================================================
// HUD AND MENUS
// ============================================================

void drawHUD() {
    // Score card
    rect(-0.87f, 0.76f, 0.36f, 0.19f,
         0.035f, 0.055f, 0.065f);

    rect(-0.87f, 0.925f, 0.36f, 0.025f,
         0.18f, 0.88f, 0.72f);

    drawText("SCORE", -0.83f, 0.905f,
             0.007f, 0.65f, 0.77f, 0.78f);

    drawText(
        std::to_string(static_cast<int>(score)),
        -0.83f, 0.845f,
        0.010f, 0.30f, 1.0f, 0.76f
    );

    // Speed card
    rect(0.53f, 0.76f, 0.34f, 0.19f,
         0.035f, 0.055f, 0.065f);

    rect(0.53f, 0.925f, 0.34f, 0.025f,
         1.0f, 0.63f, 0.18f);

    drawText("SPEED", 0.57f, 0.905f,
             0.007f, 0.78f, 0.78f, 0.80f);

    int speed = 80 + static_cast<int>(
        std::min(gameTime * 2.0f, 100.0f)
    );

    drawText(
        std::to_string(speed) + " KMH",
        0.57f, 0.845f,
        0.008f, 1.0f, 0.73f, 0.30f
    );

    // Controls hint
    drawText("A D / ARROWS", -0.87f, -0.82f,
             0.0065f, 0.74f, 0.80f, 0.78f);
}

void drawMenu() {
    // Dark overlay
    rect(-1.0f, -1.0f, 2.0f, 2.0f,
         0.025f, 0.045f, 0.055f);

    // Main panel
    rect(-0.72f, -0.45f, 1.44f, 1.10f,
         0.065f, 0.09f, 0.105f);

    // Accent strips
    rect(-0.72f, 0.60f, 1.44f, 0.025f,
         0.16f, 0.95f, 0.80f);

    rect(-0.72f, -0.45f, 1.44f, 0.018f,
         0.12f, 0.45f, 0.43f);

    drawCenteredText(
        "STREET RACER", 0.0f, 0.43f, 0.014f,
        0.25f, 1.0f, 0.82f
    );

    drawCenteredText(
        "TWO WAY TRAFFIC", 0.0f, 0.25f, 0.008f,
        1.0f, 0.70f, 0.27f
    );

    // Decorative car
    drawCar(-0.36f, -0.02f,
            0.12f, 0.53f, 1.0f, true, true);

    drawCar(0.36f, -0.02f,
            1.0f, 0.27f, 0.18f, false, false);

    drawCenteredText(
        "DODGE THE TRAFFIC", 0.0f, -0.20f, 0.007f,
        0.88f, 0.91f, 0.94f
    );

    drawCenteredText(
        "PRESS SPACE TO START", 0.0f, -0.31f, 0.007f,
        0.24f, 1.0f, 0.70f
    );
}

void drawPauseScreen() {
    rect(-1.0f, -1.0f, 2.0f, 2.0f,
         0.025f, 0.035f, 0.045f);

    rect(-0.48f, -0.22f, 0.96f, 0.44f,
         0.07f, 0.10f, 0.12f);

    drawCenteredText(
        "PAUSED", 0.0f, 0.10f, 0.015f,
        0.30f, 1.0f, 0.75f
    );

    drawCenteredText(
        "PRESS P TO RESUME", 0.0f, -0.06f, 0.007f,
        0.94f, 0.95f, 0.97f
    );
}

void drawGameOverScreen() {
    rect(-1.0f, -1.0f, 2.0f, 2.0f,
         0.025f, 0.035f, 0.045f);

    rect(-0.58f, -0.42f, 1.16f, 0.84f,
         0.075f, 0.09f, 0.11f);

    rect(-0.58f, 0.39f, 1.16f, 0.03f,
         1.0f, 0.18f, 0.16f);

    drawCenteredText(
        "GAME OVER", 0.0f, 0.27f, 0.014f,
        1.0f, 0.25f, 0.22f
    );

    drawCenteredText(
        "FINAL SCORE", 0.0f, 0.06f, 0.008f,
        0.70f, 0.78f, 0.80f
    );

    drawCenteredText(
        std::to_string(static_cast<int>(score)),
        0.0f, -0.08f, 0.015f,
        0.30f, 1.0f, 0.76f
    );

    drawCenteredText(
        "BEST " + std::to_string(static_cast<int>(bestScore)),
        0.0f, -0.22f, 0.008f,
        1.0f, 0.75f, 0.28f
    );

    drawCenteredText(
        "SPACE TO RESTART", 0.0f, -0.34f, 0.007f,
        0.92f, 0.95f, 0.97f
    );
}

// ============================================================
// GAME LOGIC
// ============================================================

bool pressedOnce(int key, bool& wasDown) {
    bool down = glfwGetKey(window, key) == GLFW_PRESS;
    bool pressed = down && !wasDown;

    wasDown = down;
    return pressed;
}

void resetGame() {
    playerLane = 1;
    playerX = PLAYER_LANES[playerLane];

    roadOffset = 0.0f;
    sceneryOffset = 0.0f;

    trafficTimer = 0.0f;
    oppositeTimer = 0.0f;
    gameTime = 0.0f;
    score = 0.0f;

    traffic.clear();
    oppositeTraffic.clear();

    state = GameState::PLAYING;
}

void spawnPlayerTraffic() {
    int lane = std::rand() % 3;
    float x = PLAYER_LANES[lane];

    // Keep some distance between cars in the same lane.
    for (const Car& car : traffic) {
        if (std::abs(car.x - x) < 0.05f && car.y > 0.48f) {
            return;
        }
    }

    const float colors[6][3] = {
        {1.0f, 0.22f, 0.17f},
        {1.0f, 0.65f, 0.12f},
        {0.68f, 0.28f, 1.0f},
        {0.20f, 0.91f, 0.54f},
        {0.95f, 0.95f, 0.98f},
        {0.15f, 0.70f, 0.95f}
    };

    int color = std::rand() % 6;

    traffic.push_back({
        x, 1.20f,
        colors[color][0],
        colors[color][1],
        colors[color][2],
        false
    });
}

void spawnOppositeTraffic() {
    int lane = std::rand() % 3;
    float x = OPPOSITE_LANES[lane];

    for (const Car& car : oppositeTraffic) {
        if (std::abs(car.x - x) < 0.05f && car.y < -0.48f) {
            return;
        }
    }

    const float colors[5][3] = {
        {1.0f, 0.24f, 0.20f},
        {0.95f, 0.83f, 0.25f},
        {0.45f, 0.76f, 1.0f},
        {0.88f, 0.43f, 0.95f},
        {0.90f, 0.92f, 0.92f}
    };

    int color = std::rand() % 5;

    oppositeTraffic.push_back({
        x, -1.22f,
        colors[color][0],
        colors[color][1],
        colors[color][2],
        false
    });
}

void updateGame(float dt) {
    bool spacePressed = pressedOnce(GLFW_KEY_SPACE, spaceWasDown);
    bool enterPressed = pressedOnce(GLFW_KEY_ENTER, enterWasDown);
    bool restartPressed = pressedOnce(GLFW_KEY_R, rWasDown);
    bool pausePressed = pressedOnce(GLFW_KEY_P, pWasDown);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (state == GameState::MENU) {
        if (spacePressed || enterPressed) {
            resetGame();
        }
        return;
    }

    if (state == GameState::GAME_OVER) {
        if (spacePressed || enterPressed || restartPressed) {
            resetGame();
        }
        return;
    }

    if (state == GameState::PAUSED) {
        if (pausePressed) {
            state = GameState::PLAYING;
        }
        return;
    }

    if (pausePressed) {
        state = GameState::PAUSED;
        return;
    }

    gameTime += dt;

    // Smooth road and scenery movement.
    float roadSpeed = 0.72f + std::min(gameTime * 0.012f, 0.55f);

    roadOffset -= roadSpeed * dt;
    if (roadOffset < 0.0f) roadOffset += 0.32f;

    sceneryOffset -= roadSpeed * 0.55f * dt;
    if (sceneryOffset < 0.0f) sceneryOffset += 0.34f;

    // Lane changes happen smoothly instead of snapping.
    bool leftDown =
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;

    bool rightDown =
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

    if (leftDown && !leftWasDown) {
        playerLane = std::max(0, playerLane - 1);
    }

    if (rightDown && !rightWasDown) {
        playerLane = std::min(2, playerLane + 1);
    }

    leftWasDown = leftDown;
    rightWasDown = rightDown;

    float targetX = PLAYER_LANES[playerLane];

    // Frame-rate-independent smoothing.
    float smoothing = 1.0f - std::exp(-9.0f * dt);
    playerX += (targetX - playerX) * smoothing;

    // Traffic in our direction moves down the screen.
    trafficTimer += dt;

    float spawnInterval = std::max(
        0.52f, 1.05f - gameTime * 0.006f
    );

    if (trafficTimer >= spawnInterval) {
        trafficTimer = 0.0f;
        spawnPlayerTraffic();
    }

    for (Car& car : traffic) {
        car.y -= roadSpeed * dt;

        if (!car.passed && car.y < PLAYER_Y - 0.30f) {
            car.passed = true;
            score += 10.0f;
        }

        bool overlapX =
            std::abs(car.x - playerX) < CAR_WIDTH * 0.85f;

        bool overlapY =
            std::abs(car.y - PLAYER_Y) < CAR_HEIGHT * 0.83f;

        if (overlapX && overlapY) {
            state = GameState::GAME_OVER;
            bestScore = std::max(bestScore, score);
        }
    }

    traffic.erase(
        std::remove_if(
            traffic.begin(), traffic.end(),
            [](const Car& car) { return car.y < -1.40f; }
        ),
        traffic.end()
    );

    // Opposite traffic travels upward, toward the top of the screen.
    oppositeTimer += dt;

    if (oppositeTimer >= 0.72f) {
        oppositeTimer = 0.0f;
        spawnOppositeTraffic();
    }

    float oppositeSpeed = 0.50f + std::min(gameTime * 0.003f, 0.25f);

    for (Car& car : oppositeTraffic) {
        car.y += oppositeSpeed * dt;
    }

    oppositeTraffic.erase(
        std::remove_if(
            oppositeTraffic.begin(), oppositeTraffic.end(),
            [](const Car& car) { return car.y > 1.40f; }
        ),
        oppositeTraffic.end()
    );
}

// ============================================================
// RENDER
// ============================================================

void drawScene() {
    vertices.clear();

    drawRoad();

    // Opposing cars face down and travel upward on screen.
    for (const Car& car : oppositeTraffic) {
        drawCar(
            car.x, car.y,
            car.r, car.g, car.b,
            false, false
        );
    }

    // Same-direction traffic.
    for (const Car& car : traffic) {
        drawCar(
            car.x, car.y,
            car.r, car.g, car.b,
            true, false
        );
    }

    // Player car.
    drawCar(
        playerX, PLAYER_Y,
        0.10f, 0.55f, 1.0f,
        true, true
    );

    if (state == GameState::PLAYING ||
        state == GameState::PAUSED) {
        drawHUD();
    }

    if (state == GameState::MENU) {
        drawMenu();
    } else if (state == GameState::PAUSED) {
        drawPauseScreen();
    } else if (state == GameState::GAME_OVER) {
        drawGameOverScreen();
    }

    glUseProgram(shaderProgram);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_DYNAMIC_DRAW
    );

    glDrawArrays(
        GL_TRIANGLES, 0,
        static_cast<GLsizei>(vertices.size())
    );

    glBindVertexArray(0);
}

// ============================================================
// MAIN
// ============================================================

int main() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW\n";
        return 1;
    }

    // macOS supports OpenGL Core Profile up to 4.1.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(
        WINDOW_WIDTH, WINDOW_HEIGHT,
        "STREET RACER 2.0",
        nullptr, nullptr
    );

    if (!window) {
        std::cerr << "Could not create the game window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!initGraphics()) {
        std::cerr << "Could not initialize graphics\n";

        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glDisable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        static double previousTime = glfwGetTime();

        double currentTime = glfwGetTime();

        float dt = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // Prevent a huge jump after moving or resizing the window.
        dt = std::min(dt, 0.05f);

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(0.04f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        updateGame(dt);
        drawScene();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
