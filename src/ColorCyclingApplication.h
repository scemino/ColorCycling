#ifndef COLORCYCLING__COLORCYCLINGAPPLICATION_H
#define COLORCYCLING__COLORCYCLINGAPPLICATION_H

#include <array>
#include <memory>
#include "Application.h"
#include "Ilbm.h"

class ColorCyclingApplication final : public Application {
public:
  explicit ColorCyclingApplication();
  ~ColorCyclingApplication() override;

protected:
  void onInit() override;
  void onImGuiRender() override;
  void onEvent(SDL_Event& event) override;
  void onRender() override;
  void onUpdate(const TimeSpan& elapsed) override;

private:
  void reshape(int x, int y) const;
  void loadLbm(const std::string &path);
  void setPalette(Ilbm &img, int idx, std::uint8_t r, std::uint8_t g, std::uint8_t b) const;

private:
  std::unique_ptr<Ilbm> m_image{};
  std::array<std::uint8_t, 256*3> m_palette{};
  int m_shaderProgram{0};
  unsigned int m_vao{0};
  unsigned int m_vbo{0}, m_ebo{0};
  unsigned int m_img_tex{0}, m_pal_tex{0};
  float time_msec{0};
  bool m_showInfo{true};
  bool m_blend{true};
  float m_speed{1.f};
  int m_currentColorIndex{-1};
};

#endif//COLORCYCLING__COLORCYCLINGAPPLICATION_H
