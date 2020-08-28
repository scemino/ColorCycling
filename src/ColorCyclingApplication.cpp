#include "ColorCyclingApplication.h"
#include "Util.h"
#include <GL/glew.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <iostream>

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

static int32_t cycleOffset(int mode, int32_t rate, int32_t rsize, int32_t msec, float speed) {
  float offs;
  float tm = (rate / 280.0f) * static_cast<float>(msec * speed) / 1000.0f;

  switch (mode) {
  case CYCLE_PINGPONG:
    offs = fmod(tm, static_cast<float>(rsize * 2));
    if (offs >= rsize)
      offs = static_cast<float>(rsize * 2) - offs;
    break;

  case CYCLE_SINE:
  case CYCLE_SINE_HALF: {
    float x = fmod(tm, static_cast<float>(rsize));
    offs = sinf((x * static_cast<float>(M_PI) * 2.0f) / static_cast<float>(rsize)) + 1.0f;
    offs *= rsize / (mode == CYCLE_SINE_HALF ? 4.0f : 2.0f);
  } break;

  default: /* normal or reverse */
    offs = tm;
  }
  return (int32_t)(offs * 256.0f);
}

static std::uint8_t lerp(std::uint8_t a, std::uint8_t b, std::int32_t xt) {
  return ((((a) << 8) + ((b) - (a)) * (xt)) >> 8);
}

static void setPalette(Ilbm &img, int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto *pptr = &img.palette[0] + idx * 3;
  pptr[0] = r;
  pptr[1] = g;
  pptr[2] = b;
}

ColorCyclingApplication::ColorCyclingApplication() = default;

ColorCyclingApplication::~ColorCyclingApplication() {
  glDeleteVertexArrays(1, &m_vao);
  glDeleteBuffers(1, &m_vbo);
  glDeleteBuffers(1, &m_ebo);
}

void ColorCyclingApplication::onInit() {
  Application::onInit();
  int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
  glCompileShader(vertexShader);
  // check for shader compile errors
  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }
  // fragment shader
  int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
  glCompileShader(fragmentShader);
  // check for shader compile errors
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
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
    glGetProgramInfoLog(m_shaderProgram, 512, nullptr, infoLog);
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

  auto tex_xsz = Util::nextPow2(fbwidth);
  auto tex_ysz = Util::nextPow2(fbheight);

  glGenTextures(1, &m_img_tex);
  glBindTexture(GL_TEXTURE_2D, m_img_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, tex_xsz, tex_ysz, 0, GL_RED, GL_UNSIGNED_BYTE, 0);

  glGenTextures(1, &m_pal_tex);
  glBindTexture(GL_TEXTURE_1D, m_pal_tex);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

  glUseProgram(m_shaderProgram);
  glUniform1i(glGetUniformLocation(m_shaderProgram, "img_tex"), 0);
  glUniform1i(glGetUniformLocation(m_shaderProgram, "pal_tex"), 1);
  glVertexAttrib2f(glGetAttribLocation(m_shaderProgram, "uvscale"), (float) fbwidth / (float) tex_xsz, (float) fbheight / (float) tex_ysz);
}

void ColorCyclingApplication::loadLbm(const std::string &path) {
  m_image = std::make_unique<Ilbm>();
  auto &image = *m_image;
  std::ifstream is(path, std::ios::binary);

  Chunk chunk{};
  std::uint8_t *temp;
  int bodyBytes = 0;
  constexpr auto chunkSize = sizeof(Chunk);
  is.read((char *) &chunk, chunkSize);

  Util::endianSwap((int32_t *) &chunk.length);

  // skip over 'PBM '
  is.seekg(4, std::ios::cur);

  while (!is.eof()) {
    is.read((char *) &chunk, chunkSize);
    Util::endianSwap((int32_t *) &chunk.length);
    if (strncmp(chunk.id, "BMHD", 4) == 0) {
      is.read((char *) &image.header, sizeof(image.header));
      Util::endianSwap(&image.header.width);
      Util::endianSwap(&image.header.height);
      Util::endianSwap(&image.header.page_width);
      Util::endianSwap(&image.header.page_height);
      image.header.width += (2 - (image.header.width % 2)) % 2;// even widths only (round up)
    } else if (strncmp(chunk.id, "CMAP", 4) == 0) {
      is.read((char *) &image.palette[0], chunk.length);
    } else if (strncmp(chunk.id, "CRNG", 4) == 0) {
      is.read((char *) &image.cycles[image.numCycles], chunk.length);
      Util::endianSwap(&image.cycles[image.numCycles].padding);
      Util::endianSwap(&image.cycles[image.numCycles].rate);
      Util::endianSwap(&image.cycles[image.numCycles].flags);
      image.numCycles++;
    } else if (strncmp(chunk.id, "BODY", 4) == 0) {
      if (image.header.compression) {
        image.image.resize(image.header.width * image.header.height);
        temp = image.image.data();
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
      memcpy(&m_palette[0], &image.palette[0], 256 * 3);
      glBindTexture(GL_TEXTURE_2D, m_img_tex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_RED, GL_UNSIGNED_BYTE, image.image.data());
      glBindTexture(GL_TEXTURE_1D, m_pal_tex);
      glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, image.palette.data());
    } else {
      if (chunk.length > 0)
        is.seekg(chunk.length, std::ios::cur);
    }
    if (chunk.length % 2 != 0)
      is.seekg(2 - (chunk.length % 2), std::ios::cur);
  }

  is.close();
}

void ColorCyclingApplication::onEvent(SDL_Event &event) {
  switch (event.type)
  case SDL_WINDOWEVENT: {
    int w, h;
    SDL_GL_GetDrawableSize(m_window.getNativeHandle(), &w, &h);
    reshape(w, h);
    break;
  case SDL_DROPFILE:
    loadLbm(event.drop.file);
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
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

  Application::onRender();
}

void ColorCyclingApplication::onUpdate(const TimeSpan &elapsed) {
  if (!m_image)
    return;

  auto &image = *m_image;
  /* for each cycling range in the image ... */
  for (auto i = 0; i < image.numCycles; i++) {
    int32_t offs, rsize, ioffs;
    int rev;

    if (!image.cycles[i].rate)
      continue;
    rsize = image.cycles[i].high - image.cycles[i].low + 1;

    time_msec += 100.0f / 60.f;

    offs = cycleOffset(image.cycles[i].flags, image.cycles[i].rate, rsize, time_msec, m_speed);

    ioffs = (offs >> 8) % rsize;

    /* reverse when rev is 2 */
    rev = image.cycles[i].flags == CYCLE_REVERSE ? 1 : 0;

    for (auto j = 0; j < rsize; j++) {
      int pidx, to, next;

      pidx = j + image.cycles[i].low;

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
      to += image.cycles[i].low;

      if (m_blend) {
        int r, g, b;
        auto fracOffs = static_cast<int32_t>(offs & 0xff);

        next += image.cycles[i].low;

        r = lerp(m_palette[to * 3], m_palette[next * 3], fracOffs);
        g = lerp(m_palette[to * 3 + 1], m_palette[next * 3 + 1], fracOffs);
        b = lerp(m_palette[to * 3 + 2], m_palette[next * 3 + 2], fracOffs);

        setPalette(image, pidx, r, g, b);
      } else {
        setPalette(image, pidx, m_palette[to * 3], m_palette[to * 3 + 1], m_palette[to * 3 + 2]);
      }
    }
  }
  glBindTexture(GL_TEXTURE_1D, m_pal_tex);
  glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGB, GL_UNSIGNED_BYTE, &image.palette[0]);
}

void ColorCyclingApplication::reshape(int x, int y) const {
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
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open", "Ctrl+O")) {
        // open Dialog Simple
        igfd::ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".LBM", ".");
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
        m_done = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Debug", "Ctrl+I", &m_showInfo, (bool) m_image);
      ImGui::EndMenu();
    }
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 200);
    ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::EndMainMenuBar();
  }

  // display
  if (igfd::ImGuiFileDialog::Instance()->FileDialog("ChooseFileDlgKey")) {
    if (igfd::ImGuiFileDialog::Instance()->IsOk) {
      auto filePathName = igfd::ImGuiFileDialog::Instance()->GetFilepathName();
      loadLbm(filePathName);
    }
    // close
    igfd::ImGuiFileDialog::Instance()->CloseDialog("ChooseFileDlgKey");
  }

  if (!m_image)
    return;
  if (m_showInfo) {
    if (ImGui::Begin("Info", &m_showInfo)) {
      auto &image = *m_image;
      if (ImGui::TreeNode("General")) {
        ImGui::Text("Image Size %dx%d", image.header.width, image.header.height);
        ImGui::Text("Origin (x=%d, y=%d)", image.header.x, image.header.y);
        ImGui::Text("%d Planes", image.header.num_planes);
        const char *masks[] = {"none", "masked", "lasso (for MacPaint)"};
        ImGui::Text("Mask: %s", masks[std::clamp((int) image.header.masking, 0, 2)]);
        const char *compressions[] = {"uncompressed", "RLE", "vertical RLE"};
        ImGui::Text("Compression: %s", compressions[std::clamp((int) image.header.compression, 0, 2)]);
        ImGui::Text("Pixel aspect: %d:%d", image.header.x_aspect, image.header.y_aspect);
        ImGui::Text("Page Size %dx%d", image.header.page_width, image.header.page_width);
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Options")) {
        ImGui::Checkbox("Cycle Blend", &m_blend);
        ImGui::DragFloat("Cycle Speed", &m_speed, 0.25f, 0.25f, 4.f);
        ImGui::TreePop();
      }

      // draw palette
      if (ImGui::TreeNode("Palette")) {
        auto palette = &image.palette[0];
        for (auto j = 0; j < 16; ++j) {
          for (auto i = 0; i < 16; ++i) {
            auto color = ImVec4(
                static_cast<float>(*palette++) / 255.0f, static_cast<float>(*palette++) / 255.0f, static_cast<float>(*palette++) / 255.0f, 1.0f);
            ImGui::ColorButton("", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker);
            ImGui::SameLine(0.f, 0.6f);
          }
          ImGui::NewLine();
        }
        ImGui::TreePop();
      }
    }
    ImGui::End();
  }
}
