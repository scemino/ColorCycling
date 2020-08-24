#include <GL/glew.h>
#include <SDL.h>
#include <array>
#include <fstream>
#include <imgui.h>
#include <imgui/examples/imgui_impl_opengl3.h>
#include <imgui/examples/imgui_impl_sdl.h>
#include <iostream>
#include <sstream>

static float fbwidth = 640;
static float fbheight = 480;

SDL_Window *m_window{nullptr};
SDL_GLContext m_glContext{nullptr};
int shaderProgram;
unsigned int VAO;
static unsigned int img_tex, pal_tex;

const char *vertexShaderSource = "#version 330 core\n"
                                 "uniform mat4 xform;\n"
                                 "layout (location = 0) in vec4 attr_vertex;\n"
                                 "in vec2 uvscale;\n"
                                 "out vec2 uv;\n"
                                 "void main()\n"
                                 "{\n"
                                 "   gl_Position = xform * attr_vertex;\n"
                                 "   uv = (attr_vertex.xy * vec2(0.5, -0.5) + 0.5) * uvscale;\n"
                                 //                                 "   uv = (vec2(attr_vertex.xy)* vec2(0.5, -0.5)+0.5);\n"
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

static const char *vsdr =
    "uniform mat4 xform;\n"
    "in vec2 uvscale;\n"
    "in vec4 attr_vertex;\n"
    "out vec2 uv;\n"
    "void main()\n"
    "{\n"
    "\tgl_Position = xform * attr_vertex;\n"
    "\tuv = (attr_vertex.xy * vec2(0.5, -0.5) + 0.5) * uvscale;\n"
    "}\n";

static unsigned int next_pow2(unsigned int x) {
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  return x + 1;
}

static void endian_swap(int32_t *value) {
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

static void endian_swap(int16_t *value) {
  unsigned char *chs;
  unsigned char temp;

  chs = (unsigned char *) value;

  temp = chs[0];
  chs[0] = chs[1];
  chs[1] = temp;
}

static void endian_swap(uint16_t *value) {
  endian_swap((int16_t *) value);
}

struct Chunk {
  char id[4];
  uint32_t length;
};

struct BitmapHeader {
  unsigned short width, height; /* raster width & height in pixels	*/
  short x, y;                   /* pixel position for this image	*/
  unsigned char num_planes;     /* # source bitplanes	*/
  unsigned char masking;
  unsigned char compression;
  unsigned char pad1;               /* unused; for consistency, put 0 here	*/
  unsigned short transparent_color; /* transparent "color number" (sort of)	*/
  unsigned char x_aspect, y_aspect; /* pixel aspect, a ratio width : height	*/
  short page_width, page_height;    /* source "page" size in pixels	*/
};

struct Crng {
  std::int16_t padding{0}; /* reserved for future use; store 0 here	*/
  std::int16_t rate{0};    /* Colour cycle rate. The units are such that a rate of 60 steps per second is represented as 214 = 16384. Lower rates can be obtained by linear scaling: for 30 steps/second, rate = 8192.	*/
  std::int16_t flags{0};   /* Flags which control the cycling of colours through the palette. If bit0 is 1, the colours should cycle, otherwise this colour register range is inactive and should have no effect. If bit1 is 0, the colours cycle upwards, i.e. each colour moves into the next index position in the colour map and the uppermost colour in the range moves down to the lowest position. If bit1 is 1, the colours cycle in the opposite direction. Only those colours between the low and high entries in the colour map should cycle. */
  std::uint8_t low{0};     /* The index of the first entry in the colour map that is part of this range.	*/
  std::uint8_t high{0};    /* The index of the last entry in the colour map that is part of this range.*/
};

struct Ilbm {
  BitmapHeader header;
  std::uint8_t *image{nullptr};
  std::uint8_t *palette{nullptr};
  std::uint8_t *palette2{nullptr};
  std::array<Crng, 256> cycles;
  std::uint8_t numCycles{0};
};

static void loadLbm(const std::string &path, Ilbm &image) {
  std::ifstream is(path, std::ios::binary);

  Chunk chunk{};
  std::uint8_t *temp = nullptr;
  int bodyBytes = 0;
  constexpr auto chunkSize = sizeof(Chunk);
  is.read((char *) &chunk, chunkSize);

  endian_swap((int32_t *) &chunk.length);

  // skip over 'PBM '
  is.seekg(4, std::ios::cur);

  while (!is.eof()) {
    is.read((char *) &chunk, chunkSize);
    endian_swap((int32_t *) &chunk.length);
    if (strncmp(chunk.id, "BMHD", 4) == 0) {
      is.read((char *) &image.header, sizeof(image.header));
      endian_swap(&image.header.width);
      endian_swap(&image.header.height);
      endian_swap(&image.header.page_width);
      endian_swap(&image.header.page_height);
      image.header.width += (2 - (image.header.width % 2)) % 2;// even widths only (round up)
    } else if (strncmp(chunk.id, "CMAP", 4) == 0) {
      if (!image.palette) {
        image.palette = (std::uint8_t *) malloc(chunk.length);
        image.palette2 = (std::uint8_t *) malloc(chunk.length);
      }
      is.read((char *) image.palette, chunk.length);
      memcpy(image.palette2, image.palette, chunk.length);
    } else if (strncmp(chunk.id, "CRNG", 4) == 0) {
      is.read((char *) &image.cycles[image.numCycles], chunk.length);
      endian_swap(&image.cycles[image.numCycles].padding);
      endian_swap(&image.cycles[image.numCycles].rate);
      endian_swap(&image.cycles[image.numCycles].flags);
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

const int CYCLE_NORMAL = 0;
const int CYCLE_REVERSE = 2;
const int CYCLE_PINGPONG = 3;
const int CYCLE_SINE_HALF = 4; /* sine -> [0, range/2] */
const int CYCLE_SINE = 5;      /* sine -> [0, range] */

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

static int32_t cycle_offset(int mode, int32_t rate, int32_t rsize, int32_t msec) {
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

static void set_palette(Ilbm &img, int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto *pptr = img.palette + idx * 3;
  pptr[0] = r;
  pptr[1] = g;
  pptr[2] = b;
}

static void update(Ilbm &img) {
  /* for each cycling range in the image ... */
  for (auto i = 0; i < img.numCycles; i++) {
    int32_t offs, rsize, ioffs;
    int rev;

    if (!img.cycles[i].rate)
      continue;
    rsize = img.cycles[i].high - img.cycles[i].low + 1;

    static float time_msec = 0;
    time_msec += 100.0f / 60.f;

    offs = cycle_offset(img.cycles[i].flags, img.cycles[i].rate, rsize, time_msec);

    ioffs = (offs >> 8) % rsize;

    /* reverse when rev is 2 */
    rev = img.cycles[i].flags == CYCLE_REVERSE ? 1 : 0;

    for (auto j = 0; j < rsize; j++) {
      int pidx, to, next;

      pidx = j + img.cycles[i].low;

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
      to += img.cycles[i].low;
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
      set_palette(img, pidx, img.palette2[to * 3], img.palette2[to * 3 + 1], img.palette2[to * 3 + 2]);
      //}
    }
  }
  glBindTexture(GL_TEXTURE_1D, pal_tex);
  glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGB, GL_UNSIGNED_BYTE, img.palette);
}

static void reshape(int x, int y) {
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

  glUseProgram(shaderProgram);
  if ((loc = glGetUniformLocation(shaderProgram, "xform")) >= 0) {
    glUniformMatrix4fv(loc, 1, GL_FALSE, xform);
  }

  auto err = glGetError();
  assert(err == GL_NO_ERROR);
}

static void cleanup() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(m_glContext);
  SDL_DestroyWindow(m_window);
  SDL_Quit();
}

static void init(Ilbm &img) {

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::ostringstream ss;
    ss << "Error when initializing SDL (error=" << SDL_GetError() << ")";
    throw std::runtime_error(ss.str());
  }

  // Decide GL+GLSL versions
#if __APPLE__
  // GL 3.2 Core + GLSL 150
  const char *glsl_version = "#version 150";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);// Always required on Mac
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  m_window = SDL_CreateWindow("SDL/OpenGL Color cycling", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);

  // setup OpenGL
  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext) {
    std::ostringstream ss;
    ss << "Error when creating GL context (error=" << SDL_GetError() << ")";
    throw std::runtime_error(ss.str());
  }

  SDL_GL_MakeCurrent(m_window, m_glContext);
  SDL_GL_SetSwapInterval(1);

  auto err = glewInit();
  if (GLEW_OK != err) {
    std::ostringstream ss;
    ss << "Error when initializing glew " << glewGetErrorString(err);
    throw std::runtime_error(ss.str());
  }

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
  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);
  // check for linking errors
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
              << infoLog << std::endl;
  }
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  // set up vertex data (and buffer(s)) and configure vertex attributes
  // ------------------------------------------------------------------
  float vertices[] = {
      1.0f, 1.0f, 0.0f, 0.0f,  // top right
      1.0f, -1.0f, 0.0f, 0.0f, // bottom right
      -1.0f, -1.0f, 0.0f, 0.0f,// bottom left
      -1.0f, 1.0f, 0.0f, 0.0f  // top left
  };
  unsigned int indices[] = {
      // note that we start from 0!
      0, 1, 3,// first triangle
      1, 2, 3 // second triangle
  };

  unsigned int VBO, EBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);
  // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

  // position attribute
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);

  auto tex_xsz = next_pow2(fbwidth);
  auto tex_ysz = next_pow2(fbheight);

  glGenTextures(1, &img_tex);
  glBindTexture(GL_TEXTURE_2D, img_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, tex_xsz, tex_ysz, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_RED, GL_UNSIGNED_BYTE, img.image);

  glGenTextures(1, &pal_tex);
  glBindTexture(GL_TEXTURE_1D, pal_tex);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, img.palette);

  glUseProgram(shaderProgram);
  glUniform1i(glGetUniformLocation(shaderProgram, "img_tex"), 0);
  glUniform1i(glGetUniformLocation(shaderProgram, "pal_tex"), 1);
  glVertexAttrib2f(glGetAttribLocation(shaderProgram, "uvscale"), (float) fbwidth / (float) tex_xsz, (float) fbheight / (float) tex_ysz);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer bindings
  // window is the SDL_Window*
  // contex is the SDL_GLContext
  ImGui_ImplSDL2_InitForOpenGL(m_window, m_glContext);
  ImGui_ImplOpenGL3_Init(glsl_version);

  err = glGetError();
  assert(err == GL_NO_ERROR);
}

static void render(Ilbm &img) {
  // render
  // ------
  glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // bind textures on corresponding texture units
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, img_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_1D, pal_tex);

  // draw our first triangle
  glUseProgram(shaderProgram);
  glBindVertexArray(VAO);// seeing as we only have a single VAO there's no need to bind it every time, but we'll do so to keep things a bit more organized
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  // Render dear imgui
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame(m_window);
  ImGui::NewFrame();

  ImGui::Begin("Debug");// Pass a pointer to our bool variable
  // (the window will have a closing button
  // that will clear the bool when clicked)
  ImGui::Text("Size %dx%d", img.header.width, img.header.height);
  std::ostringstream s;
  s << (int) img.numCycles << " cycles";
  if (ImGui::TreeNode(s.str().c_str())) {
    for (auto i = 0; i < img.numCycles; i++) {
      std::ostringstream cycleName;
      cycleName << "cycle " << (i + 1);
      if (ImGui::TreeNode(cycleName.str().c_str())) {
        ImGui::Text("Rate %d", img.cycles[i].rate);
        ImGui::Text("Low %d", img.cycles[i].low);
        ImGui::Text("High %d", img.cycles[i].high);
        ImGui::TreePop();
      }
    }
    ImGui::TreePop();
  }

  // draw palette
  if (ImGui::TreeNode("Palette")) {
    auto palette = img.palette;
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

  // imgui render
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  SDL_GL_SwapWindow(m_window);

  auto err = glGetError();
  assert(err == GL_NO_ERROR);
}

static void renderLoop(Ilbm &img) {
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        done = true;
      }
      if (event.type == SDL_WINDOWEVENT) {
        int w, h;
        SDL_GL_GetDrawableSize(m_window, &w, &h);
        reshape(w, h);
      } else if (event.type == SDL_DROPFILE) {// In case if dropped file
        img.numCycles = 0;
        loadLbm(event.drop.file, img);
        glBindTexture(GL_TEXTURE_2D, img_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_RED, GL_UNSIGNED_BYTE, img.image);
        glBindTexture(GL_TEXTURE_1D, pal_tex);
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, img.palette);
        SDL_free(event.drop.file);
      }
    }
    update(img);
    render(img);
  }
}

static void usage(const char* exe) {
  std::cout << "usage: " << exe << " file.lbm" << std::endl;
}

int main(int argc, const char **argv) {
  if (argc != 2) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  Ilbm img;
  loadLbm(argv[1], img);
  init(img);
  renderLoop(img);
  cleanup();
  return EXIT_SUCCESS;
}