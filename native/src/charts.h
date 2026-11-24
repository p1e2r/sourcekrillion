// Port of public/presentation.mjs: tier icons, the dive log and the score distribution.
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

std::string iconFor(const std::string& tier);
std::string tierImage(const std::string& tier, const std::string& cls = "");
std::string diveChart(const nlohmann::json& results);
std::string distributionSvg(const std::vector<int>& dist, int score, int population);
float dropMs(const std::string& tier);
