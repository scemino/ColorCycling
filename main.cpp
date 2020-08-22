#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>

static float fbwidth = 640;
static float fbheight = 480;

SDL_Window *m_window{nullptr};
SDL_GLContext m_glContext{nullptr};
static unsigned int img_tex, pal_tex, prog;
static unsigned int create_shader(unsigned int type, const char *src);

static const char *vsdr =
    "uniform mat4 xform;\n"
    "uniform vec2 uvscale;\n"
    "attribute vec4 attr_vertex;\n"
    "varying vec2 uv;\n"
    "void main()\n"
    "{\n"
    "\tgl_Position = xform * attr_vertex;\n"
    "\tuv = (attr_vertex.xy * vec2(0.5, -0.5) + 0.5) * uvscale;\n"
    "}\n";

static const char *psdr =
    "uniform sampler2D img_tex;\n"
    "uniform sampler1D pal_tex;\n"
    "varying vec2 uv;\n"
    "void main()\n"
    "{\n"
    "\tfloat cidx = texture2D(img_tex, uv).x;\n"
    "\tvec3 color = texture1D(pal_tex, cidx).xyz;\n"
    "\tgl_FragColor.xyz = color;\n"
    "\tgl_FragColor.a = 1.0;\n"
    "}\n";

static float verts[] = {
    -1, -1, 1, -1, 1, 1,
    -1, -1, 1, 1, -1, 1};
static unsigned int vbo;

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

void endian_swap(int16_t *value) {
  unsigned char *chs;
  unsigned char temp;

  chs = (unsigned char *) value;

  temp = chs[0];
  chs[0] = chs[1];
  chs[1] = temp;
}

void endian_swap(uint16_t *value) {
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

static unsigned int create_program(const char *vsdr, const char *psdr) {
  unsigned int vs, ps, prog;
  int status;

  if (!(vs = create_shader(GL_VERTEX_SHADER, vsdr))) {
    return 0;
  }
  if (!(ps = create_shader(GL_FRAGMENT_SHADER, psdr))) {
    glDeleteShader(vs);
    return 0;
  }

  prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, ps);
  glLinkProgram(prog);

  glGetProgramiv(prog, GL_LINK_STATUS, &status);
  if (!status) {
    fprintf(stderr, "failed to link shader program\n");
    glDeleteProgram(prog);
    prog = 0;
  }
  return prog;
}

static unsigned int create_shader(unsigned int type, const char *src) {
  unsigned int sdr;
  int status, info_len;

  sdr = glCreateShader(type);
  glShaderSource(sdr, 1, &src, 0);
  glCompileShader(sdr);

  glGetShaderiv(sdr, GL_COMPILE_STATUS, &status);
  if (!status) {
    fprintf(stderr, "failed to compile %s shader\n", type == GL_VERTEX_SHADER ? "vertex" : "pixel");
  }

  glGetShaderiv(sdr, GL_INFO_LOG_LENGTH, &info_len);
  if (info_len) {
    char *buf = (char *) alloca(info_len + 1);
    glGetShaderInfoLog(sdr, info_len, 0, buf);
    buf[info_len] = 0;
    if (buf[0]) {
      fprintf(stderr, "compiler output:\n%s\n", buf);
    }
  }

  if (!status) {
    glDeleteShader(sdr);
    sdr = 0;
  }
  return sdr;
}

static void init(Ilbm &img) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::ostringstream ss;
    ss << "Error when initializing SDL (error=" << SDL_GetError() << ")";
    throw std::runtime_error(ss.str());
  }

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  auto window_flags = (SDL_WindowFlags)(
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  m_window = SDL_CreateWindow("SDL/OpenGL Color cycling", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 640, 480, window_flags);

  // setup OpenGL
  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext) {
    std::ostringstream ss;
    ss << "Error when creating GL context (error=" << SDL_GetError() << ")";
    throw std::runtime_error(ss.str());
  }

  auto err = glewInit();
  if (GLEW_OK != err) {
    std::ostringstream ss;
    ss << "Error when initializing glew " << glewGetErrorString(err);
    throw std::runtime_error(ss.str());
  }

  SDL_GL_MakeCurrent(m_window, m_glContext);
  SDL_GL_SetSwapInterval(1);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);

  auto tex_xsz = next_pow2(fbwidth);
  auto tex_ysz = next_pow2(fbheight);

  glGenTextures(1, &img_tex);
  glBindTexture(GL_TEXTURE_2D, img_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, tex_xsz, tex_ysz, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbwidth, fbheight, GL_LUMINANCE, GL_UNSIGNED_BYTE, img.image);

  glGenTextures(1, &pal_tex);
  glBindTexture(GL_TEXTURE_1D, pal_tex);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, img.palette);

  if (!(prog = create_program(vsdr, psdr))) {
    return;
  }
  glBindAttribLocation(prog, 0, "attr_vertex");
  glLinkProgram(prog);
  glUseProgram(prog);

  int loc;
  if ((loc = glGetUniformLocation(prog, "img_tex")) >= 0) {
    glUniform1i(loc, 0);
  }
  if ((loc = glGetUniformLocation(prog, "pal_tex")) >= 0) {
    glUniform1i(loc, 1);
  }
  if ((loc = glGetUniformLocation(prog, "uvscale")) >= 0) {
    glUniform2f(loc, (float) fbwidth / (float) tex_xsz, (float) fbheight / (float) tex_ysz);
  }
  err = glGetError();
  assert(err == GL_NO_ERROR);
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

static void render() {
  glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, img_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_1D, pal_tex);
  glActiveTexture(GL_TEXTURE0);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  // swap
  SDL_GL_SwapWindow(m_window);
  auto err = glGetError();
  assert(err == GL_NO_ERROR);
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

  glUseProgram(prog);
  if ((loc = glGetUniformLocation(prog, "xform")) >= 0) {
    glUniformMatrix4fv(loc, 1, GL_FALSE, xform);
  }
}

static bool pollEvent(SDL_Event &event) {
  auto result = SDL_PollEvent(&event);
  if (result && event.type == SDL_WINDOWEVENT) {
    int w, h;
    SDL_GL_GetDrawableSize(m_window, &w, &h);
    reshape(w, h);
    return false;
  }
  return result;
}

static void renderLoop(Ilbm &img) {
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (pollEvent(event)) {
      if (event.type == SDL_QUIT) {
        done = true;
      }
    }
    update(img);
    render();
  }
}

static void deinit() {
  SDL_GL_DeleteContext(m_glContext);
  SDL_DestroyWindow(m_window);
  SDL_Quit();
}

int main(int, char **) {
  Ilbm img;
  loadLbm("V08.LBM", img);
  init(img);
  renderLoop(img);
  deinit();
}