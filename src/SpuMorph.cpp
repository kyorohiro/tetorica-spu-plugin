// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuMorph.h"
#include <cmath>
#include <stdexcept>

namespace tetorica {
void validateMorphEndpoints(const MorphEndpoints& endpoints) {
  validatePcmCycle(endpoints.a);validatePcmCycle(endpoints.b);
}
PcmCycle blendMorph(const MorphEndpoints& endpoints,double amount) {
  validateMorphEndpoints(endpoints);
  if(!std::isfinite(amount)||amount<0||amount>1)
    throw std::invalid_argument("Software morph amount must be finite and in [0,1]");
  if(amount==0)return endpoints.a;
  if(amount==1)return endpoints.b;
  PcmCycle result{};
  for(unsigned i=0;i<result.size();++i)
    result[i]=static_cast<float>(double(endpoints.a[i])+(double(endpoints.b[i])-endpoints.a[i])*amount);
  return result;
}
} // namespace tetorica
