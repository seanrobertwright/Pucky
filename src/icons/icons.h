// Provider icons generated from assets/icons/png by LVGL's LVGLImage.py (RGB565A8).
#pragma once
#include <lvgl.h>

LV_IMAGE_DECLARE(icon_claude_96);
LV_IMAGE_DECLARE(icon_codex_96);
LV_IMAGE_DECLARE(icon_openrouter_96);
LV_IMAGE_DECLARE(icon_gemini_96);
LV_IMAGE_DECLARE(icon_antigravity_96);
LV_IMAGE_DECLARE(icon_zai_96);
LV_IMAGE_DECLARE(icon_deepseek_96);
LV_IMAGE_DECLARE(icon_kiro_96);
LV_IMAGE_DECLARE(icon_copilot_96);
LV_IMAGE_DECLARE(icon_claude_64);
LV_IMAGE_DECLARE(icon_codex_64);
LV_IMAGE_DECLARE(icon_openrouter_64);
LV_IMAGE_DECLARE(icon_gemini_64);
LV_IMAGE_DECLARE(icon_antigravity_64);
LV_IMAGE_DECLARE(icon_zai_64);
LV_IMAGE_DECLARE(icon_deepseek_64);
LV_IMAGE_DECLARE(icon_kiro_64);
LV_IMAGE_DECLARE(icon_copilot_64);

struct ProviderStyle {
  const char *id;
  const lv_image_dsc_t *icon96;
  const lv_image_dsc_t *icon64;
  uint32_t accent;
};

// Accent = ring colour when status is ok.
static const ProviderStyle PROVIDER_STYLES[] = {
  {"claude",     &icon_claude_96,     &icon_claude_64,     0xD97757},
  {"codex",      &icon_codex_96,      &icon_codex_64,      0x10A37F},
  {"openrouter", &icon_openrouter_96, &icon_openrouter_64, 0x8B7CF6},
  {"antigravity", &icon_antigravity_96, &icon_antigravity_64, 0x4E8DF5},
  {"gemini",     &icon_gemini_96,     &icon_gemini_64,     0x4E8DF5},   // retired CLI, kept for old hosts
  {"zai",        &icon_zai_96,        &icon_zai_64,        0xE0E0E0},
  {"deepseek",   &icon_deepseek_96,   &icon_deepseek_64,   0x4D6BFE},
  {"kiro",       &icon_kiro_96,       &icon_kiro_64,       0x9B5CF6},
  {"copilot",    &icon_copilot_96,    &icon_copilot_64,    0xE6EDF3},
};

inline const ProviderStyle *providerStyle(const char *id) {
  for (const auto &s : PROVIDER_STYLES)
    if (strcmp(s.id, id) == 0) return &s;
  return nullptr;
}
