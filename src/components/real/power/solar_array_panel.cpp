﻿/*
 * @file solar_array_panel.cpp
 * @brief Component emulation of Solar Array Panel
 */

#include "solar_array_panel.hpp"

#include <components/real/power/csv_scenario_interface.hpp>
#include <environment/global/clock_generator.hpp>

SAP::SAP(const int prescaler, ClockGenerator* clock_generator, int component_id, int number_of_series, int number_of_parallel, double cell_area,
         libra::Vector<3> normal_vector, double cell_efficiency, double transmission_efficiency, const SolarRadiationPressureEnvironment* srp,
         const LocalCelestialInformation* local_celestial_information, double compo_step_time)
    : Component(prescaler, clock_generator),
      component_id_(component_id),
      number_of_series_(number_of_series),
      number_of_parallel_(number_of_parallel),
      cell_area_(cell_area),
      normal_vector_(libra::Normalize(normal_vector)),
      cell_efficiency_(cell_efficiency),
      transmission_efficiency_(transmission_efficiency),
      srp_(srp),
      local_celestial_information_(local_celestial_information),
      compo_step_time_(compo_step_time) {
  voltage_ = 0.0;
  power_generation_ = 0.0;
}

SAP::SAP(const int prescaler, ClockGenerator* clock_generator, int component_id, int number_of_series, int number_of_parallel, double cell_area,
         libra::Vector<3> normal_vector, double cell_efficiency, double transmission_efficiency, const SolarRadiationPressureEnvironment* srp,
         double compo_step_time)
    : Component(prescaler, clock_generator),
      component_id_(component_id),
      number_of_series_(number_of_series),
      number_of_parallel_(number_of_parallel),
      cell_area_(cell_area),
      normal_vector_(libra::Normalize(normal_vector)),
      cell_efficiency_(cell_efficiency),
      transmission_efficiency_(transmission_efficiency),
      srp_(srp),
      compo_step_time_(compo_step_time) {
  voltage_ = 0.0;
  power_generation_ = 0.0;
}

SAP::SAP(ClockGenerator* clock_generator, int component_id, int number_of_series, int number_of_parallel, double cell_area,
         libra::Vector<3> normal_vector, double cell_efficiency, double transmission_efficiency, const SolarRadiationPressureEnvironment* srp,
         const LocalCelestialInformation* local_celestial_information)
    : Component(10, clock_generator),
      component_id_(component_id),
      number_of_series_(number_of_series),
      number_of_parallel_(number_of_parallel),
      cell_area_(cell_area),
      normal_vector_(libra::Normalize(normal_vector)),
      cell_efficiency_(cell_efficiency),
      transmission_efficiency_(transmission_efficiency),
      srp_(srp),
      local_celestial_information_(local_celestial_information),
      compo_step_time_(0.1) {
  voltage_ = 0.0;
  power_generation_ = 0.0;
}

SAP::SAP(const SAP& obj)
    : Component(obj),
      component_id_(obj.component_id_),
      number_of_series_(obj.number_of_series_),
      number_of_parallel_(obj.number_of_parallel_),
      cell_area_(obj.cell_area_),
      normal_vector_(obj.normal_vector_),
      cell_efficiency_(obj.cell_efficiency_),
      transmission_efficiency_(obj.transmission_efficiency_),
      srp_(obj.srp_),
      local_celestial_information_(obj.local_celestial_information_),
      compo_step_time_(obj.compo_step_time_) {
  voltage_ = 0.0;
  power_generation_ = 0.0;
}

SAP::~SAP() {}

double SAP::GetPowerGeneration() const { return power_generation_; }

void SAP::SetVoltage_V(const double voltage) { voltage_ = voltage; }

std::string SAP::GetLogHeader() const {
  std::string str_tmp = "";
  std::string component_name = "sap" + std::to_string(component_id_) + "_";
  str_tmp += WriteScalar(component_name + "generated_power", "W");
  return str_tmp;
}

std::string SAP::GetLogValue() const {
  std::string str_tmp = "";
  str_tmp += WriteScalar(power_generation_);
  return str_tmp;
}

void SAP::MainRoutine(int time_count) {
  if (CsvScenarioInterface::IsCsvScenarioEnabled()) {
    double time_query = compo_step_time_ * time_count;
    const auto solar_constant = srp_->GetSolarConstant_W_m2();
    libra::Vector<3> sun_direction_body = CsvScenarioInterface::GetSunDirectionBody(time_query);
    libra::Vector<3> normalized_sun_direction_body = libra::Normalize(sun_direction_body);
    power_generation_ = cell_efficiency_ * transmission_efficiency_ * solar_constant * (int)CsvScenarioInterface::GetSunFlag(time_query) *
                        cell_area_ * number_of_parallel_ * number_of_series_ * InnerProduct(normal_vector_, normalized_sun_direction_body);
  } else {
    const auto power_density = srp_->GetPowerDensity_W_m2();
    libra::Vector<3> sun_pos_b = local_celestial_information_->GetPositionFromSpacecraft_b_m("SUN");
    libra::Vector<3> sun_dir_b = libra::Normalize(sun_pos_b);
    power_generation_ = cell_efficiency_ * transmission_efficiency_ * power_density * cell_area_ * number_of_parallel_ * number_of_series_ *
                        InnerProduct(normal_vector_, sun_dir_b);
    // TODO: Improve implementation. For example, update IV curve with sun direction and calculate generated power
  }
  if (power_generation_ < 0) power_generation_ = 0.0;
}
