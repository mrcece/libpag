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

#include "CGTypefaceCache.h"

#include <utility>

namespace pag {
static CGTypefaceCache& Get() {
  static CGTypefaceCache& gCache = *new CGTypefaceCache();
  return gCache;
}

void CGTypefaceCache::Add(std::shared_ptr<Typeface> typeface) {
  Get().add(std::move(typeface));
}

std::shared_ptr<Typeface> CGTypefaceCache::FindByPredicate(
    const std::function<bool(Typeface*)>& predicate) {
  return Get().findByPredicate(predicate);
}

static constexpr size_t kTypefaceCacheCount = 1024;
void CGTypefaceCache::add(std::shared_ptr<Typeface> typeface) {
  if (typefaces.size() >= kTypefaceCacheCount) {
    purge();
  }
  typefaces.emplace_back(std::move(typeface));
}

std::shared_ptr<Typeface> CGTypefaceCache::findByPredicate(
    const std::function<bool(Typeface*)>& predicate) {
  for (const auto& typeface : typefaces) {
    if (predicate(typeface.get())) {
      return typeface;
    }
  }
  return nullptr;
}

void CGTypefaceCache::purge() {
  auto numToPurge = kTypefaceCacheCount >> 2;
  auto iter = typefaces.begin();
  while (iter != typefaces.end()) {
    if (iter->unique()) {
      iter = typefaces.erase(iter);
      if (--numToPurge == 0) {
        return;
      }
    } else {
      iter++;
    }
  }
}
}  // namespace pag
