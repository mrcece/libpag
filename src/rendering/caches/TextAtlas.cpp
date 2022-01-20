/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "TextAtlas.h"
#include "RenderCache.h"
#include "gpu/Canvas.h"
#include "gpu/Surface.h"

namespace pag {
struct AtlasTextRun {
  tgfx::Paint paint;
  tgfx::Font textFont = {};
  std::vector<tgfx::GlyphID> glyphIDs;
  std::vector<tgfx::Point> positions;
};

class Atlas {
 public:
  static std::unique_ptr<Atlas> Make(tgfx::Context* context, float scale,
                                     const std::vector<GlyphHandle>& glyphs, int maxTextureSize,
                                     bool alphaOnly = true);

  bool getLocator(const GlyphHandle& glyph, TextStyle style, AtlasLocator* locator) const;

  size_t memoryUsage() const;

 private:
  struct Page {
    std::vector<AtlasTextRun> textRuns;
    int width = 0;
    int height = 0;
    std::shared_ptr<tgfx::Texture> texture;
  };

  void initPages(const std::vector<GlyphHandle>& glyphs, float scale, int maxTextureSize);

  void draw(tgfx::Context* context, float scale, bool alphaOnly);

  std::vector<Page> pages;
  std::unordered_map<tgfx::BytesKey, AtlasLocator, tgfx::BytesHasher> glyphLocators;

  friend class TextAtlas;
};

class RectanglePack {
 public:
  explicit RectanglePack(float scale) {
    padding = static_cast<int>(ceil(static_cast<float>(kDefaultPadding) / scale));
    reset();
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  Point addRect(int w, int h) {
    w += padding;
    h += padding;
    auto area = (_width - x) * (_height - y);
    if ((x + w - _width) * y > area || (y + h - _height) * x > area) {
      if (_width <= _height) {
        x = _width;
        y = padding;
        _width += w;
      } else {
        x = padding;
        y = _height;
        _height += h;
      }
    }
    auto point = Point::Make(static_cast<float>(x), static_cast<float>(y));
    if (x + w - _width < y + h - _height) {
      x += w;
      _height = std::max(_height, y + h);
    } else {
      y += h;
      _width = std::max(_width, x + w);
    }
    return point;
  }

  void reset() {
    _width = padding;
    _height = padding;
    x = padding;
    y = padding;
  }

 private:
  static constexpr int kDefaultPadding = 3;
  int padding = kDefaultPadding;
  int _width = 0;
  int _height = 0;
  int x = 0;
  int y = 0;
};

std::unique_ptr<Atlas> Atlas::Make(tgfx::Context* context, float scale,
                                   const std::vector<GlyphHandle>& glyphs, int maxTextureSize,
                                   bool alphaOnly) {
  if (glyphs.empty()) {
    return nullptr;
  }
  if (glyphs[0]->getFont().getSize() * scale > 256.f) {
    return nullptr;
  }
  auto atlas = std::make_unique<Atlas>();
  atlas->initPages(glyphs, scale, maxTextureSize);
  atlas->draw(context, scale, alphaOnly);
  return atlas;
}

size_t Atlas::memoryUsage() const {
  size_t usage = 0;
  for (auto& page : pages) {
    usage += page.texture->memoryUsage();
  }
  return usage;
}

static tgfx::PaintStyle ToTGFXPaintStyle(TextStyle style) {
  switch (style) {
    case TextStyle::StrokeAndFill:
    case TextStyle::Fill:
      return tgfx::PaintStyle::Fill;
    case TextStyle::Stroke:
      return tgfx::PaintStyle::Stroke;
  }
}

static AtlasTextRun CreateTextRun(const GlyphHandle& glyph) {
  AtlasTextRun textRun;
  textRun.textFont = glyph->getFont();
  textRun.paint.setStyle(ToTGFXPaintStyle(glyph->getStyle()));
  if (glyph->getStyle() == TextStyle::Stroke) {
    textRun.paint.setStrokeWidth(glyph->getStrokeWidth());
  }
  return textRun;
}

static void ComputeStyleKey(tgfx::BytesKey* styleKey, const GlyphHandle& glyph) {
  styleKey->write(static_cast<uint32_t>(glyph->getStyle()));
  styleKey->write(glyph->getStrokeWidth());
  styleKey->write(glyph->getFont().getTypeface()->uniqueID());
}

void Atlas::initPages(const std::vector<GlyphHandle>& glyphs, float scale, int maxTextureSize) {
  std::vector<tgfx::BytesKey> styleKeys = {};
  std::vector<AtlasTextRun> textRuns = {};
  auto maxPageSize = static_cast<int>(std::floor(static_cast<float>(maxTextureSize) / scale));
  RectanglePack pack(scale);
  Page page;
  int pageIndex = 0;
  for (auto& glyph : glyphs) {
    tgfx::BytesKey styleKey = {};
    ComputeStyleKey(&styleKey, glyph);
    auto iter = std::find(styleKeys.begin(), styleKeys.end(), styleKey);
    AtlasTextRun* textRun;
    if (iter == styleKeys.end()) {
      styleKeys.push_back(styleKey);
      textRuns.push_back(CreateTextRun(glyph));
      textRun = &textRuns.back();
    } else {
      auto index = iter - styleKeys.begin();
      textRun = &textRuns[index];
    }
    auto glyphWidth = static_cast<int>(glyph->getBounds().width());
    auto glyphHeight = static_cast<int>(glyph->getBounds().height());
    int strokeWidth = 0;
    if (glyph->getStyle() == TextStyle::Stroke) {
      strokeWidth = static_cast<int>(ceil(glyph->getStrokeWidth()));
    }
    auto x = glyph->getBounds().x() - static_cast<float>(strokeWidth);
    auto y = glyph->getBounds().y() - static_cast<float>(strokeWidth);
    auto width = glyphWidth + strokeWidth * 2;
    auto height = glyphHeight + strokeWidth * 2;
    auto packWidth = pack.width();
    auto packHeight = pack.height();
    auto point = pack.addRect(width, height);
    if (pack.width() > maxPageSize || pack.height() > maxPageSize) {
      page.textRuns = std::move(textRuns);
      page.width = static_cast<int>(ceil(static_cast<float>(packWidth) * scale));
      page.height = static_cast<int>(ceil(static_cast<float>(packHeight) * scale));
      pages.push_back(std::move(page));
      styleKeys.clear();
      textRuns = {};
      page = {};
      pack.reset();
      point = pack.addRect(width, height);
      pageIndex++;
    }
    textRun->glyphIDs.push_back(glyph->getGlyphID());
    textRun->positions.push_back({-x + point.x, -y + point.y});
    AtlasLocator locator;
    locator.pageIndex = pageIndex;
    locator.location = tgfx::Rect::MakeXYWH(point.x, point.y, static_cast<float>(width),
                                            static_cast<float>(height));
    locator.location.scale(scale, scale);
    tgfx::BytesKey bytesKey;
    glyph->computeAtlasKey(&bytesKey, glyph->getStyle());
    glyphLocators[bytesKey] = locator;
  }
  page.textRuns = std::move(textRuns);
  page.width = static_cast<int>(ceil(static_cast<float>(pack.width()) * scale));
  page.height = static_cast<int>(ceil(static_cast<float>(pack.height()) * scale));
  pages.push_back(std::move(page));
}

void DrawTextRun(tgfx::Canvas* canvas, const std::vector<AtlasTextRun>& textRuns, float scale) {
  auto totalMatrix = canvas->getMatrix();
  for (auto& textRun : textRuns) {
    canvas->setMatrix(totalMatrix);
    canvas->concat(tgfx::Matrix::MakeScale(scale));
    auto glyphs = &textRun.glyphIDs[0];
    auto positions = &textRun.positions[0];
    canvas->drawGlyphs(glyphs, positions, textRun.glyphIDs.size(), textRun.textFont, textRun.paint);
  }
  canvas->setMatrix(totalMatrix);
}

void Atlas::draw(tgfx::Context* context, float scale, bool alphaOnly) {
  for (auto& page : pages) {
    auto surface = tgfx::Surface::Make(context, page.width, page.height, alphaOnly);
    auto atlasCanvas = surface->getCanvas();
    DrawTextRun(atlasCanvas, page.textRuns, scale);
    page.texture = surface->getTexture();
  }
}

bool Atlas::getLocator(const GlyphHandle& glyph, TextStyle style, AtlasLocator* locator) const {
  tgfx::BytesKey bytesKey;
  glyph->computeAtlasKey(&bytesKey, style);
  auto iter = glyphLocators.find(bytesKey);
  if (iter == glyphLocators.end()) {
    return false;
  }
  if (locator) {
    *locator = iter->second;
  }
  return true;
}

std::unique_ptr<TextAtlas> TextAtlas::Make(const TextGlyphs* textGlyphs, RenderCache* renderCache,
                                           float scale) {
  auto context = renderCache->getContext();
  auto maxTextureSize = context->caps()->maxTextureSize;
  scale = scale * textGlyphs->maxScale();
  auto maskAtlas =
      Atlas::Make(context, scale, textGlyphs->maskAtlasGlyphs(), maxTextureSize).release();
  if (maskAtlas == nullptr) {
    return nullptr;
  }
  auto colorAtlas =
      Atlas::Make(context, scale, textGlyphs->colorAtlasGlyphs(), maxTextureSize, false).release();
  return std::unique_ptr<TextAtlas>(new TextAtlas(textGlyphs->id(), maskAtlas, colorAtlas, scale));
}

TextAtlas::~TextAtlas() {
  delete maskAtlas;
  delete colorAtlas;
}

bool TextAtlas::getLocator(const GlyphHandle& glyph, TextStyle style, AtlasLocator* locator) const {
  if (glyph->getFont().getTypeface()->hasColor()) {
    if (colorAtlas && colorAtlas->getLocator(glyph, style, locator)) {
      locator->pageIndex += maskAtlas->pages.size();
      return true;
    }
    return false;
  }
  return maskAtlas->getLocator(glyph, style, locator);
}

std::shared_ptr<tgfx::Texture> TextAtlas::getAtlasTexture(size_t pageIndex) const {
  if (maskAtlas->pages.size() > pageIndex) {
    return maskAtlas->pages[pageIndex].texture;
  }
  pageIndex = pageIndex - maskAtlas->pages.size();
  if (colorAtlas && colorAtlas->pages.size() > pageIndex) {
    return colorAtlas->pages[pageIndex].texture;
  }
  return nullptr;
}

size_t TextAtlas::memoryUsage() const {
  size_t usage = 0;
  if (maskAtlas) {
    usage += maskAtlas->memoryUsage();
  }
  if (colorAtlas) {
    usage += colorAtlas->memoryUsage();
  }
  return usage;
}
}  // namespace pag
