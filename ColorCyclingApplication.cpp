#include "ColorCyclingApplication.h"
#include <GL/glew.h>
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <imgui/examples/imgui_impl_opengl3.h>
#include <iostream>
#include <sstream>

const float fbwidth = 640;
const float fbheight = 480;

const char *vertexShaderSource = "#version 330 core\n"
                                 "uniform mat4 xform;\n"
                                 "layout (location = 0) in vec4 attr_vertex;\n"
                                 "in vec2 uvscale;\n"
                                 "out vec2 uv;\n"
                                 "void main()\n"
                                 "{\n"
                                 "   gl_Position = xform * attr_vertex;\n"
                                 "   uv = (attr_vertex.xy * vec2(0.5, -0.5) + 0.5) * uvscale;\n"
                                 "}\0";
const char *fragmentShaderSource = "#version 330 core\n"
                                   "out vec4 FragColor;\n"
                                   "in vec2 uv;\n"
                                   "uniform sampler2D img_tex;\n"
                                   "uniform sampler1D pal_tex;\n"
                                   "void main()\n"
                                   "{\n"
                                   "  float cidx = texture(img_tex, uv).x;\n"
                                   "  vec3 color = texture(pal_tex, cidx).xyz;\n"
                                   "  FragColor.xyz = color;\n"
                                   "  FragColor.a = 1.0;\n"
                                   "}\n\0";

const int CYCLE_NORMAL = 0;
const int CYCLE_REVERSE = 2;
const int CYCLE_PINGPONG = 3;
const int CYCLE_SINE_HALF = 4; /* sine -> [0, range/2] */
const int CYCLE_SINE = 5;      /* sine -> [0, range] */

constexpr float vertices[] = {
    1.0f, 1.0f, 0.0f, 0.0f,  // top right
    1.0f, -1.0f, 0.0f, 0.0f, // bottom right
    -1.0f, -1.0f, 0.0f, 0.0f,// bottom left
    -1.0f, 1.0f, 0.0f, 0.0f  // top left
};

constexpr unsigned int indices[] = {
    // note that we start from 0!
    0, 1, 3,// first triangle
    1, 2, 3 // second triangle
};

#ifdef __GNUC__
#define CALC_TIME(res, anim_rate, time_msec)               \
  asm volatile(                                            \
      "\n\tmull %1" /* edx:eax <- eax(rate << 8) * msec */ \
      "\n\tmovl $280000, %%ebx"                            \
      "\n\tdivl %%ebx" /* eax <- edx:eax / ebx */          \
      : "=a"(res)                                          \
      : "g"((uint32_t)(time_msec)), "a"((anim_rate) << 8)  \
      : "ebx", "edx")
#endif /* __GNUC__ */

static int32_t cycleOffset(int mode, int32_t rate, int32_t rsize, int32_t msec) {
  int32_t offs, tm;

  CALC_TIME(tm, rate, msec);

  switch (mode) {
  case CYCLE_PINGPONG:
    rsize <<= 8; /* rsize -> 24.8 fixed point */
    offs = tm % (rsize * 2);
    if (offs > rsize)
      offs = (rsize * 2) - offs;
    rsize >>= 8; /* back to 32.0 */
    break;

  case CYCLE_SINE:
  case CYCLE_SINE_HALF: {
    float t = (float) tm / 256.0; /* convert fixed24.8 -> float */
    float x = fmod(t, (float) (rsize * 2));
    float foffs = sin((x * M_PI * 2.0) / (float) rsize) + 1.0;
    if (mode == CYCLE_SINE_HALF) {
      foffs *= rsize / 4.0;
    } else {
      foffs *= rsize / 2.0;
    }
    offs = (int32_t)(foffs * 256.0); /* convert float -> fixed24.8 */
  } break;

  default: /* normal or reverse */
    offs = tm;
  }

  return offs;
}

static void setPalette(Ilbm &img, int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto *pptr = &img.palette[0] + idx * 3;
  pptr[0] = r;
  pptr[1] = g;
  pptr[2] = b;
}

static unsigned int nextPow2(unsigned int x) {
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  return x + 1;
}

static void endianSwap(int32_t *value) {
  unsigned char *chs;
  unsigned char temp;

  chs = (unsigned char *) value;

  temp = chs[0];
  chs[0] = chs[3];
  chs[3] = temp;
  temp = chs[1];
  chs[1] = chs[2];
  chs[2] = temp;
}

static void endianSwap(int16_t *value) {
  unsigned char *chs;
  unsigned char temp;

  chs = (unsigned char *) value;

  temp = chs[0];
  chs[0] = chs[1];
  chs[1] = temp;
}

static void endianSwap(uint16_t *value) {
  endianSwap((int16_t *) value);
}

static void loadLbm(const std::string &path, Ilbm &image) {
  std::ifstream is(path, std::ios::binary);

  Chunk chunk{};
  std::uint8_t *temp = nullptr;
  int bodyBytes = 0;
  constexpr auto chunkSize = sizeof(Chunk);
  is.read((char *) &chunk, chunkSize);

  endianSwap((int32_t *) &chunk.length);

  // skip over 'PBM '
  is.seekg(4, std::ios::cur);

  while (!is.eof()) {
    is.read((char *) &chunk, chunkSize);
    endianSwap((int32_t *) &chunk.length);
    if (strncmp(chunk.id, "BMHD", 4) == 0) {
      is.read((char *) &image.header, sizeof(image.header));
      endianSwap(&image.header.width);
      endianSwap(&image.header.height);
      endianSwap(&image.header.page_width);
      endianSwap(&image.header.page_height);
      image.header.width += (2 - (image.header.width % 2)) % 2;// even widths only (round up)
    } else if (strncmp(chunk.id, "CMAP", 4) == 0) {
      is.read((char *) &image.palette[0], chunk.length);
    } else if (strncmp(chunk.id, "CRNG", 4) == 0) {
      is.read((char *) &image.cycles[image.numCycles], chunk.length);
      endianSwap(&image.cycles[image.numCycles].padding);
      endianSwap(&image.cycles[image.numCycles].rate);
      endianSwap(&image.cycles[image.numCycles].flags);
      image.numCycles++;
    } else if (strncmp(chunk.id, "BODY", 4) == 0) {
      if (image.header.compression) {
        if (!temp) {
          image.image = temp = (std::uint8_t *) malloc(image.header.width * image.header.height);
        }
        signed char sdata;
        auto len = chunk.length;
        is.read((char *) &sdata, 1);
        while (len > 0 && !is.eof()) {
          len--;
          /* ByteRun1 decompression */

          /* [0..127]   : followed by n+1 bytes of data. */
          if (sdata >= 0) {
            auto i = sdata + 1;
            for (auto j = 0; j < i; j++) {
              char udata;
              is.read(&udata, 1);
              len--;
              if (is.eof())
                break;
              *temp++ = udata;
              bodyBytes++;
            }
          }
          /* [-1..-127] : followed by byte to be repeated (-n)+1 times*/
          else if (sdata <= -1 && sdata >= -127) {
            auto i = (-sdata) + 1;
            char udata;
            is.read(&udata, 1);
            len--;
            for (auto j = 0; j < i; j++) {
              *temp++ = udata;
              bodyBytes++;
            }
          }
          /* -128	   : NOOP. */
          is.read((char *) &sdata, 1);
        }
      }
      break;
    } else {
      if (chunk.length > 0)
        is.seekg(chunk.length, std::ios::cur);
    }
    if (chunk.length % 2 != 0)
      is.seekg(2 - (chunk.length % 2), std::ios::cur);
  }

  is.close();
}

ColorCyclingApplication::ColorCyclingApplication(const std::string &path) {
  loadLbm(path, m_image);
  memcpy(&m_palette[0], &m_image.palette[0], 256 * 3);
}

ColorCyclingApplication::~ColorCyclingApplication() {
  glDeleteVertexArrays(1, &m_vao);
  glDeleteBuffers(1, &m_vbo);
  glDeleteBuffers(1, &m_ebo);
}

void ColorCyclingApplication::onInit() {
  Application::onInit();
  int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);
  // check for shader compile errors
  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }
  // fragment shader
  int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);
  // check for shader compile errors
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }
  // link shaders
  m_shaderProgram = glCreateProgram();
  glAttachShader(m_shaderProgram, vertexShader);
  glAttachShader(m_shaderProgram, fragmentShader);
  glLinkProgram(m_shaderProgram);
  // check for linking errors
  glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(m_shaderProgram, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
              << infoLog << std::endl;
  }
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glGenBuffers(1, &m_ebo);
  // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

  // position attribute
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);

  auto tex_xsz = nextPow2(fbwidth);
  auto tex_ysz = nextPow2(fbheight);

  glGenTextures(1, &m_img_tex);
  glBindTexture(GL_TEXTURE_2D, m_img_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, tex_xsz, tex_ysz, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_RED, GL_UNSIGNED_BYTE, m_image.image);

  glGenTextures(1, &m_pal_tex);
  glBindTexture(GL_TEXTURE_1D, m_pal_tex);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, &m_image.palette[0]);

  glUseProgram(m_shaderProgram);
  glUniform1i(glGetUniformLocation(m_shaderProgram, "img_tex"), 0);
  glUniform1i(glGetUniformLocation(m_shaderProgram, "pal_tex"), 1);
  glVertexAttrib2f(glGetAttribLocation(m_shaderProgram, "uvscale"), (float) fbwidth / (float) tex_xsz, (float) fbheight / (float) tex_ysz);
}

void ColorCyclingApplication::onEvent(SDL_Event &event) {
  switch (event.type)
  case SDL_WINDOWEVENT: {
    int w, h;
    SDL_GL_GetDrawableSize(m_window.getNativeHandle(), &w, &h);
    reshape(w, h);
    break;
  case SDL_DROPFILE:
    // In case if dropped file
    m_image.numCycles = 0;
    loadLbm(event.drop.file, m_image);
    glBindTexture(GL_TEXTURE_2D, m_img_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_RED, GL_UNSIGNED_BYTE, m_image.image);
    glBindTexture(GL_TEXTURE_1D, m_pal_tex);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, &m_image.palette[0]);
    SDL_free(event.drop.file);
    break;
  case SDL_KEYDOWN:
    if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
      m_done = true;
    }
    break;
  }
}

void ColorCyclingApplication::onRender() {
  glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // bind textures on corresponding texture units
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_img_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_1D, m_pal_tex);

  // draw our first triangle
  glUseProgram(m_shaderProgram);
  glBindVertexArray(m_vao);// seeing as we only have a single m_vao there's no need to bind it every time, but we'll do so to keep things a bit more organized
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  Application::onRender();
}

void ColorCyclingApplication::onUpdate(const TimeSpan &elapsed) {
  /* for each cycling range in the image ... */
  for (auto i = 0; i < m_image.numCycles; i++) {
    int32_t offs, rsize, ioffs;
    int rev;

    if (!m_image.cycles[i].rate)
      continue;
    rsize = m_image.cycles[i].high - m_image.cycles[i].low + 1;

    static float time_msec = 0;
    time_msec += 100.0f / 60.f;

    offs = cycleOffset(m_image.cycles[i].flags, m_image.cycles[i].rate, rsize, time_msec);

    ioffs = (offs >> 8) % rsize;

    /* reverse when rev is 2 */
    rev = m_image.cycles[i].flags == CYCLE_REVERSE ? 1 : 0;

    for (auto j = 0; j < rsize; j++) {
      int pidx, to, next;

      pidx = j + m_image.cycles[i].low;

      if (rev) {
        to = (j + ioffs) % rsize;
        next = (to + 1) % rsize;
      } else {
        if ((to = (j - ioffs) % rsize) < 0) {
          to += rsize;
        }
        if ((next = to - 1) < 0) {
          next += rsize;
        }
      }
      to += m_image.cycles[i].low;
      //
      //      if(blend) {
      //        int r, g, b;
      //        int32_t frac_offs = offs & 0xff;
      //
      //        next += img->range[i].low;
      //
      //        r = LERP_FIXED_T(img->palette[to].r, img->palette[next].r, frac_offs);
      //        g = LERP_FIXED_T(img->palette[to].g, img->palette[next].g, frac_offs);
      //        b = LERP_FIXED_T(img->palette[to].b, img->palette[next].b, frac_offs);
      //
      //        set_palette(pidx, r, g, b);
      //      } else {
      setPalette(m_image, pidx, m_palette[to * 3], m_palette[to * 3 + 1], m_palette[to * 3 + 2]);
      //}
    }
  }
  glBindTexture(GL_TEXTURE_1D, m_pal_tex);
  glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGB, GL_UNSIGNED_BYTE, &m_image.palette[0]);
}

void ColorCyclingApplication::reshape(int x, int y) {
  int loc;
  float aspect = (float) x / (float) y;
  float fbaspect = (float) fbwidth / (float) fbheight;
  float xform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  glViewport(0, 0, x, y);

  if (aspect > fbaspect) {
    xform[0] = fbaspect / aspect;
  } else if (fbaspect > aspect) {
    xform[5] = aspect / fbaspect;
  }

  glUseProgram(m_shaderProgram);
  if ((loc = glGetUniformLocation(m_shaderProgram, "xform")) >= 0) {
    glUniformMatrix4fv(loc, 1, GL_FALSE, xform);
  }

  auto err = glGetError();
  assert(err == GL_NO_ERROR);
}

void ColorCyclingApplication::onImGuiRender() {
  ImGui::Begin("Debug");
  if (ImGui::TreeNode("Info")) {
    ImGui::Text("Image Size %dx%d", m_image.header.width, m_image.header.height);
    ImGui::Text("Origin (x=%d, y=%d)", m_image.header.x, m_image.header.y);
    ImGui::Text("%d Planes", m_image.header.num_planes);
    const char *masks[] = {"none", "masked", "lasso (for MacPaint)"};
    ImGui::Text("Mask: %s", masks[std::clamp((int)m_image.header.masking, 0, 2)]);
    const char *compressions[] = {"uncompressed", "RLE", "vertical RLE"};
    ImGui::Text("Compression: %s", compressions[std::clamp((int)m_image.header.compression, 0, 2)]);
    ImGui::Text("Pixel aspect: %d:%d", m_image.header.x_aspect, m_image.header.y_aspect);
    ImGui::Text("Page Size %dx%d", m_image.header.page_width, m_image.header.page_width);
    ImGui::TreePop();
  }

  // draw palette
  if (ImGui::TreeNode("Palette")) {
    auto palette = &m_image.palette[0];
    for (auto j = 0; j < 16; ++j) {
      for (auto i = 0; i < 16; ++i) {
        auto color = ImVec4(
            (*palette++) / 255.0f, (*palette++) / 255.0f, (*palette++) / 255.0f, 1.0f);
        ImGui::ColorButton("", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine(0.f, 0.6f);
      }
      ImGui::NewLine();
    }
    ImGui::TreePop();
  }

  ImGui::End();
}