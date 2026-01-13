#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Mat4 {
  float m[16];
};

static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

static Vec3 normalize(Vec3 v) {
  float len = std::sqrt(dot(v, v));
  if (len <= 0.00001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

static float radians(float degrees) {
  return degrees * 3.1415926535f / 180.0f;
}

static Mat4 perspective(float fovDegrees, float aspect, float nearZ, float farZ) {
  float f = 1.0f / std::tan(radians(fovDegrees) * 0.5f);
  Mat4 out = {};
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = (farZ + nearZ) / (nearZ - farZ);
  out.m[11] = -1.0f;
  out.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
  return out;
}

static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
  Vec3 f = normalize(center - eye);
  Vec3 r = normalize(cross(f, up));
  Vec3 u = cross(r, f);

  Mat4 out = {};
  out.m[0] = r.x;
  out.m[1] = u.x;
  out.m[2] = -f.x;
  out.m[3] = 0.0f;

  out.m[4] = r.y;
  out.m[5] = u.y;
  out.m[6] = -f.y;
  out.m[7] = 0.0f;

  out.m[8] = r.z;
  out.m[9] = u.z;
  out.m[10] = -f.z;
  out.m[11] = 0.0f;

  out.m[12] = -dot(r, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = dot(f, eye);
  out.m[15] = 1.0f;

  return out;
}

static Vec3 cameraPos = {0.0f, 0.0f, 3.0f};
static Vec3 cameraFront = {0.0f, 0.0f, -1.0f};
static Vec3 cameraUp = {0.0f, 1.0f, 0.0f};
static Vec3 cameraRight = {1.0f, 0.0f, 0.0f};
static Vec3 worldUp = {0.0f, 1.0f, 0.0f};
static float yaw = -90.0f;
static float pitch = 0.0f;
static bool firstMouse = true;
static double lastX = 0.0;
static double lastY = 0.0;

static void updateCameraVectors() {
  Vec3 front;
  front.x = std::cos(radians(yaw)) * std::cos(radians(pitch));
  front.y = std::sin(radians(pitch));
  front.z = std::sin(radians(yaw)) * std::cos(radians(pitch));
  cameraFront = normalize(front);
  cameraRight = normalize(cross(cameraFront, worldUp));
  cameraUp = normalize(cross(cameraRight, cameraFront));
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
  (void)window;
  glViewport(0, 0, width, height);
}

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
  (void)window;
  if (firstMouse) {
    lastX = xpos;
    lastY = ypos;
    firstMouse = false;
  }

  float xoffset = static_cast<float>(xpos - lastX);
  float yoffset = static_cast<float>(lastY - ypos);
  lastX = xpos;
  lastY = ypos;

  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  yaw += xoffset;
  pitch += yoffset;

  if (pitch > 89.0f) pitch = 89.0f;
  if (pitch < -89.0f) pitch = -89.0f;

  updateCameraVectors();
}

static void processInput(GLFWwindow* window, float deltaTime) {
  float velocity = 3.0f * deltaTime;

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }

  Vec3 flatFront = normalize({cameraFront.x, 0.0f, cameraFront.z});
  Vec3 flatRight = normalize(cross(flatFront, worldUp));

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    cameraPos = cameraPos + flatFront * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    cameraPos = cameraPos - flatFront * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    cameraPos = cameraPos - flatRight * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    cameraPos = cameraPos + flatRight * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
    cameraPos = cameraPos + worldUp * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
    cameraPos = cameraPos - worldUp * velocity;
  }
}

static void drawCube(float size) {
  float s = size * 0.5f;

  glBegin(GL_QUADS);

  glColor3f(1.0f, 0.2f, 0.2f);
  glVertex3f(-s, -s, s);
  glVertex3f(s, -s, s);
  glVertex3f(s, s, s);
  glVertex3f(-s, s, s);

  glColor3f(0.2f, 1.0f, 0.2f);
  glVertex3f(-s, -s, -s);
  glVertex3f(-s, s, -s);
  glVertex3f(s, s, -s);
  glVertex3f(s, -s, -s);

  glColor3f(0.2f, 0.2f, 1.0f);
  glVertex3f(-s, s, -s);
  glVertex3f(-s, s, s);
  glVertex3f(s, s, s);
  glVertex3f(s, s, -s);

  glColor3f(1.0f, 1.0f, 0.2f);
  glVertex3f(-s, -s, -s);
  glVertex3f(s, -s, -s);
  glVertex3f(s, -s, s);
  glVertex3f(-s, -s, s);

  glColor3f(0.2f, 1.0f, 1.0f);
  glVertex3f(s, -s, -s);
  glVertex3f(s, s, -s);
  glVertex3f(s, s, s);
  glVertex3f(s, -s, s);

  glColor3f(1.0f, 0.2f, 1.0f);
  glVertex3f(-s, -s, -s);
  glVertex3f(-s, -s, s);
  glVertex3f(-s, s, s);
  glVertex3f(-s, s, -s);

  glEnd();
}

int main() {
  if (!glfwInit()) {
    std::fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "D Cube", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetCursorPosCallback(window, mouseCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  updateCameraVectors();

  glEnable(GL_DEPTH_TEST);
  glClearColor(0.08f, 0.09f, 0.12f, 1.0f);

  float lastFrame = static_cast<float>(glfwGetTime());

  while (!glfwWindowShouldClose(window)) {
    float currentFrame = static_cast<float>(glfwGetTime());
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    processInput(window, deltaTime);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width == 0 || height == 0) {
      glfwPollEvents();
      continue;
    }

    float aspect = static_cast<float>(width) / static_cast<float>(height);
    Mat4 proj = perspective(60.0f, aspect, 0.1f, 100.0f);
    Mat4 view = lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    glPushMatrix();
    drawCube(1.0f);
    glPopMatrix();

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
