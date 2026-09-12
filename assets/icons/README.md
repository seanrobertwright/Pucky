# Provider icons

Source: [@lobehub/icons-static-svg](https://github.com/lobehub/lobe-icons) (MIT license), fetched
2026-09-12 from `https://cdn.jsdelivr.net/npm/@lobehub/icons-static-svg@latest/icons/<name>.svg`.
Brand marks remain the property of their respective owners.

- `svg/` — originals. Mono icons use `currentColor`.
- `png/` — rendered with resvg at 64 px and 96 px on a transparent background; mono icons rendered white
  for the dark UI. Re-render: `node <scratch>/svgrender/render.js svg png 64,96`.

Planned mapping (provider -> icon):

| Provider    | Icon              | Notes                                  |
|-------------|-------------------|----------------------------------------|
| Claude Code | `claude-color`    | orange starburst (`claude` = white)    |
| Codex       | `openai`          | OpenAI blossom, white                  |
| OpenRouter  | `openrouter`      | white                                  |
| Gemini      | `gemini-color`    | gradient spark (`gemini` = white)      |
| Antigravity | `antigravity-color` | gradient arch (`antigravity` = white); replaces Gemini CLI |
| DeepSeek    | `deepseek-color`  | blue whale (`deepseek` = white)        |
| z.AI (GLM)  | `zai`             | white "Z"; `zhipu*` is the parent co.  |

For LVGL, convert the chosen PNGs to C arrays (RGB565A8) with LVGL's image converter:

    magick -background none -density 384 svg/<name>.svg -resize 96x96 -gravity center -extent 96x96 png/<name>-96.png
    python .pio/libdeps/rotary21/lvgl/scripts/LVGLImage.py --ofmt C --cf RGB565A8 -o src/icons icon_<id>_96.png

(the input file name becomes the C symbol, so copy/rename the PNG to `icon_<id>_<size>.png` first).
