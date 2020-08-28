#ifndef COLORCYCLING__ILBM_H
#define COLORCYCLING__ILBM_H

#include <array>
#include <cstdint>
#include <vector>

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
  std::vector<std::uint8_t> image;
  std::array<std::uint8_t, 256 * 3> palette;
  std::array<Crng, 256> cycles;
  std::uint8_t numCycles{0};
};

#endif//COLORCYCLING__ILBM_H
