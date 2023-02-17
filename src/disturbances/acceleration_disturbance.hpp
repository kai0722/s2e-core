/**
 * @file acceleration_disturbance.hpp
 * @brief Abstract class for a disturbance which generate acceleration only (not force)
 */

#ifndef S2E_DISTURBANCES_ACCELERATION_DISTURBANCE_HPP_
#define S2E_DISTURBANCES_ACCELERATION_DISTURBANCE_HPP_

#include "../dynamics/dynamics.hpp"
#include "../environment/local/local_environment.hpp"
#include "disturbance.hpp"

/**
 * @class AccelerationDisturbance
 * @brief Abstract class for a disturbance which generate acceleration only (not force)
 */
class AccelerationDisturbance : public Disturbance, public ILoggable {
 public:
  /**
   * @fn AccelerationDisturbance
   * @brief Constructor
   * @param [in] is_calculation_enabled: Calculation flag
   */
  AccelerationDisturbance(const bool is_calculation_enabled = true) : Disturbance(is_calculation_enabled) {}
  /**
   * @fn ~AccelerationDisturbance
   * @brief Destructor
   */
  virtual ~AccelerationDisturbance() {}

  /**
   * @fn UpdateIfEnabled
   * @brief Update calculated disturbance when the calculation flag is true
   */
  virtual inline void UpdateIfEnabled(const LocalEnvironment& local_environment, const Dynamics& dynamics) {
    if (is_calculation_enabled_) {
      Update(local_environment, dynamics);
    } else {
      acceleration_b_m_s2_ *= 0.0;
      acceleration_i_m_s2_ *= 0.0;
    }
  }

  /**
   * @fn Update
   * @brief Pure virtual function to define the disturbance calculation
   */
  virtual void Update(const LocalEnvironment& local_environment, const Dynamics& dynamics) = 0;
};

#endif  // S2E_DISTURBANCES_ACCELERATION_DISTURBANCE_HPP_
