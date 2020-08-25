#include "ColorCyclingApplication.h"
#include <iostream>

static void usage(const char* exe) {
  std::cout << "usage: " << exe << " file.lbm" << std::endl;
}

int main(int argc, const char **argv) {
  if (argc != 2) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  ColorCyclingApplication app(argv[1]);
  app.run();
  return EXIT_SUCCESS;
}