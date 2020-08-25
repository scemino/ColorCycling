#ifndef COLORCYCLING__COLORCYCLINGAPPLICATION_H
#define COLORCYCLING__COLORCYCLINGAPPLICATION_H

#include <array>
#include "Application.h"
#include "Ilbm.h"

class ColorCyclingApplication final : public Application {
public:
  explicit ColorCyclingApplication(const std::string& path);
  ~ColorCyclingApplication();

protected:
  void onInit() override;
  void onImGuiRender() override;
  void onEvent(SDL_Event& event) override;
  void onRender() override;
  void onUpdate(const TimeSpan& elapsed) override;

private:
  void reshape(int x, int y);

private:
  Ilbm m_image{};
  int m_shaderProgram{0};
  unsigned int m_vao{0};
  unsigned int m_vbo{0}, m_ebo{0};
  unsigned int m_img_tex{0}, m_pal_tex{0};
  std::array<std::uint8_t, 256*3> m_palette;
};

#endif//COLORCYCLING__COLORCYCLINGAPPLICATION_H
