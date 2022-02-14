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

#include "Shape.h"
#include "gpu/Canvas.h"
#include "gpu/Shader.h"
#include "pag/file.h"
#include "rendering/utils/TGFXTypes.h"

namespace pag {
std::shared_ptr<Graphic> Shape::MakeFrom(const Path& path, Color4f color) {
  if (path.isEmpty()) {
    return nullptr;
  }
  Paint paint = {};
  paint.setColor(color);
  return std::shared_ptr<Graphic>(new Shape(path, paint));
}

std::shared_ptr<Graphic> Shape::MakeFrom(const Path& path, const GradientPaint& gradient) {
  if (path.isEmpty()) {
    return nullptr;
  }
  std::shared_ptr<Shader> shader;
  if (gradient.gradientType == GradientFillType::Linear) {
    shader = Shader::MakeLinearGradient(gradient.startPoint, gradient.endPoint, gradient.colors,
                                        gradient.positions);
  } else {
    auto radius = Point::Distance(gradient.startPoint, gradient.endPoint);
    shader = Shader::MakeRadialGradient(gradient.startPoint, radius, gradient.colors,
                                        gradient.positions);
  }
  if (!shader) {
    shader = Shader::MakeColorShader(gradient.colors.back());
  }
  Paint paint = {};
  paint.setShader(shader);
  return std::shared_ptr<Graphic>(new Shape(path, paint));
}

Shape::Shape(Path path, Paint paint) : path(std::move(path)), paint(paint) {
}

void Shape::measureBounds(Rect* bounds) const {
  *bounds = path.getBounds();
}

bool Shape::hitTest(RenderCache*, float x, float y) {
  return path.contains(x, y);
}

bool Shape::getPath(Path* result) const {
  if (paint.getAlpha() != 1.0f) {
    return false;
  }
  auto shader = paint.getShader();
  if (shader && !shader->isOpaque()) {
    return false;
  }
  result->addPath(path);
  return true;
}

void Shape::prepare(RenderCache*) const {
}

void Shape::draw(Canvas* canvas, RenderCache*) const {
  canvas->drawPath(path, paint);
}

}  // namespace pag