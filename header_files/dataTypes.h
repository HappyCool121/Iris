//
// Created by Imari on 13/3/26.
//

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

const float PI = 3.14159265359f;

struct ray {
  glm::vec3 origin = {0, 0, 0};
  glm::vec3 d = {0, 0, 0};
};
