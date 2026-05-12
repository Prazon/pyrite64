/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "particleSystemAsset.h"

namespace
{
  nlohmann::json vec3ToJson(const glm::vec3 &v) {
    return nlohmann::json::array({v.x, v.y, v.z});
  }
  nlohmann::json vec4ToJson(const glm::vec4 &v) {
    return nlohmann::json::array({v.x, v.y, v.z, v.w});
  }
  glm::vec3 vec3FromJson(const nlohmann::json &j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
  }
  glm::vec4 vec4FromJson(const nlohmann::json &j, glm::vec4 fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
  }
}

std::string Project::Assets::ParticleSystemAsset::serialize() const
{
  nlohmann::json doc{};
  doc["uuid"]         = uuid;
  doc["version"]      = version;
  doc["simModel"]     = (int)simModel;
  doc["particleType"] = (int)particleType;
  doc["spriteUUID"]   = spriteUUID;

  doc["maxParticles"] = maxParticles;
  doc["spawnRate"]    = spawnRate;
  doc["burstCount"]   = burstCount;
  doc["loop"]         = loop;
  doc["duration"]     = duration;
  doc["isRotating"]   = isRotating;
  doc["noRng"]        = noRng;

  doc["shape"]        = (int)shape;
  doc["sphereRadius"] = sphereRadius;
  doc["boxExtents"]   = vec3ToJson(boxExtents);
  doc["discRadius"]   = discRadius;
  doc["discNormal"]   = vec3ToJson(discNormal);

  doc["lifetimeMin"]      = lifetimeMin;
  doc["lifetimeMax"]      = lifetimeMax;
  doc["startScaleMin"]    = startScaleMin;
  doc["startScaleMax"]    = startScaleMax;
  doc["startVelDir"]      = vec3ToJson(startVelDir);
  doc["startVelSpeedMin"] = startVelSpeedMin;
  doc["startVelSpeedMax"] = startVelSpeedMax;
  doc["gravity"]          = vec3ToJson(gravity);
  doc["drag"]             = drag;

  doc["startColor"]     = vec4ToJson(startColor);
  doc["endColor"]       = vec4ToJson(endColor);
  doc["colorOverLife"]  = colorOverLife;
  doc["sizeOverLife"]   = sizeOverLife;

  doc["animFps"]        = animFps;

  return doc.dump(2);
}

void Project::Assets::ParticleSystemAsset::deserialize(const std::string &raw)
{
  if (raw.empty()) {
    *this = {};
    return;
  }
  auto doc = nlohmann::json::parse(raw, nullptr, false);
  if (!doc.is_object()) {
    *this = {};
    return;
  }

  ParticleSystemAsset def{};
  uuid         = doc.value<uint64_t>("uuid", 0);
  version      = doc.value<int32_t>("version", 1);
  simModel     = (SimModel)doc.value<int32_t>("simModel", SIM_CPU_PER_PARTICLE);
  particleType = (ParticleType)doc.value<int32_t>("particleType", PT_TEX_A_S16);
  spriteUUID   = doc.value<uint64_t>("spriteUUID", 0);

  maxParticles = doc.value<uint32_t>("maxParticles", def.maxParticles);
  spawnRate    = doc.value<float>("spawnRate", def.spawnRate);
  burstCount   = doc.value<uint32_t>("burstCount", def.burstCount);
  loop         = doc.value<bool>("loop", def.loop);
  duration     = doc.value<float>("duration", def.duration);
  isRotating   = doc.value<bool>("isRotating", def.isRotating);
  noRng        = doc.value<bool>("noRng", def.noRng);

  shape        = (ShapeKind)doc.value<int32_t>("shape", def.shape);
  sphereRadius = doc.value<float>("sphereRadius", def.sphereRadius);
  boxExtents   = vec3FromJson(doc.value("boxExtents", nlohmann::json{}), def.boxExtents);
  discRadius   = doc.value<float>("discRadius", def.discRadius);
  discNormal   = vec3FromJson(doc.value("discNormal", nlohmann::json{}), def.discNormal);

  lifetimeMin       = doc.value<float>("lifetimeMin", def.lifetimeMin);
  lifetimeMax       = doc.value<float>("lifetimeMax", def.lifetimeMax);
  startScaleMin     = doc.value<float>("startScaleMin", def.startScaleMin);
  startScaleMax     = doc.value<float>("startScaleMax", def.startScaleMax);
  startVelDir       = vec3FromJson(doc.value("startVelDir", nlohmann::json{}), def.startVelDir);
  startVelSpeedMin  = doc.value<float>("startVelSpeedMin", def.startVelSpeedMin);
  startVelSpeedMax  = doc.value<float>("startVelSpeedMax", def.startVelSpeedMax);
  gravity           = vec3FromJson(doc.value("gravity", nlohmann::json{}), def.gravity);
  drag              = doc.value<float>("drag", def.drag);

  startColor    = vec4FromJson(doc.value("startColor", nlohmann::json{}), def.startColor);
  endColor      = vec4FromJson(doc.value("endColor", nlohmann::json{}), def.endColor);
  colorOverLife = doc.value<bool>("colorOverLife", def.colorOverLife);
  sizeOverLife  = doc.value<bool>("sizeOverLife", def.sizeOverLife);

  animFps       = doc.value<float>("animFps", def.animFps);
}
