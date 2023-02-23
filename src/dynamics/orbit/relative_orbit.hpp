/**
 * @file relative_orbit.hpp
 * @brief Class to propagate relative orbit
 */

#ifndef S2E_DYNAMICS_ORBIT_RELATIVE_ORBIT_HPP_
#define S2E_DYNAMICS_ORBIT_RELATIVE_ORBIT_HPP_

#include <library/math/ordinary_differential_equation.hpp>
#include <library/orbit/relative_orbit_models.hpp>
#include <simulation/multiple_spacecraft/relative_information.hpp>
#include <string>

#include "orbit.hpp"

/**
 * @class RelativeOrbit
 * @brief Class to propagate relative orbit
 */
class RelativeOrbit : public Orbit, public libra::ODE<6> {
 public:
  /**
   * @enum RelativeOrbitUpdateMethod
   * @brief Relative orbit update method
   */
  typedef enum { RK4 = 0, STM = 1 } RelativeOrbitUpdateMethod;

  /**
   * @fn RelativeOrbit
   * @brief Constructor
   * @param [in] celestial_information: Celestial information
   * @param [in] timestep: Time step [sec]
   * @param [in] reference_spacecraft_id: Reference satellite ID
   * @param [in] initial_relative_position_lvlh: Initial value of relative position at the LVLH frame of reference satellite
   * @param [in] initial_relative_velocity_lvlh: Initial value of relative velocity at the LVLH frame of reference satellite
   * @param [in] update_method: Update method
   * @param [in] relative_dynamics_model_type: Relative dynamics model type
   * @param [in] stm_model_type: State transition matrix type
   * @param [in] rel_info: Relative information
   */
  RelativeOrbit(const CelestialInformation* celestial_information, double mu_m3_s2, double timestep, int reference_spacecraft_id,
                libra::Vector<3> initial_relative_position_lvlh, libra::Vector<3> initial_relative_velocity_lvlh,
                RelativeOrbitUpdateMethod update_method, RelativeOrbitModel relative_dynamics_model_type, STMModel stm_model_type,
                RelativeInformation* rel_info);
  /**
   * @fn ~RelativeOrbit
   * @brief Destructor
   */
  ~RelativeOrbit();

  // Override Orbit
  /**
   * @fn Propagate
   * @brief Propagate orbit
   * @param [in] end_time_s: End time of simulation [sec]
   * @param [in] current_time_jd: Current Julian day [day]
   */
  virtual void Propagate(double end_time_s, double current_time_jd);

  // Override ODE
  /**
   * @fn RHS
   * @brief Right Hand Side of ordinary difference equation
   * @param [in] t: Time as independent variable
   * @param [in] state: Position and velocity as state vector
   * @param [out] rhs: Output of the function
   */
  virtual void RHS(double t, const Vector<6>& state, Vector<6>& rhs);

 private:
  double mu_m3_s2_;                       //!< Gravity constant of the center body [m3/s2]
  unsigned int reference_spacecraft_id_;  //!< Reference satellite ID
  double propagation_time_s_;             //!< Simulation current time for numerical integration by RK4 [sec]
  double propagation_step_s_;             //!< Step width for RK4 [sec]

  libra::Matrix<6, 6> system_matrix_;  //!< System matrix
  libra::Matrix<6, 6> stm_;            //!< State transition matrix

  libra::Vector<6> initial_state_;               //!< Initial state (Position and Velocity)
  libra::Vector<3> relative_position_lvlh_m_;    //!< Relative position in the LVLH frame
  libra::Vector<3> relative_velocity_lvlh_m_s_;  //!< Relative velocity in the LVLH frame

  RelativeOrbitUpdateMethod update_method_;          //!< Update method
  RelativeOrbitModel relative_dynamics_model_type_;  //!< Relative dynamics model type
  STMModel stm_model_type_;                          //!< State Transition Matrix model type
  RelativeInformation* relative_information_;        //!< Relative information

  /**
   * @fn InitializeState
   * @brief Initialize state variables
   * @param [in] initial_relative_position_lvlh: Initial value of relative position at the LVLH frame of reference satellite
   * @param [in] initial_relative_velocity_lvlh: Initial value of relative velocity at the LVLH frame of reference satellite
   * @param [in] mu_m3_s2: Gravity constant of the center body [m3/s2]
   * @param [in] init_time: Initialize time [sec]
   */
  void InitializeState(libra::Vector<3> initial_relative_position_lvlh, libra::Vector<3> initial_relative_velocity_lvlh, double mu_m3_s2,
                       double init_time = 0);
  /**
   * @fn CalculateSystemMatrix
   * @brief Calculate system matrix
   * @param [in] relative_dynamics_model_type: Relative dynamics model type
   * @param [in] reference_sat_orbit: Orbit information of reference satellite
   * @param [in] mu_m3_s2: Gravity constant of the center body [m3/s2]
   */
  void CalculateSystemMatrix(RelativeOrbitModel relative_dynamics_model_type, const Orbit* reference_sat_orbit, double mu_m3_s2);
  /**
   * @fn CalculateSTM
   * @brief Calculate State Transition Matrix
   * @param [in] stm_model_type: STM model type
   * @param [in] reference_sat_orbit: Orbit information of reference satellite
   * @param [in] mu_m3_s2: Gravity constant of the center body [m3/s2]
   * @param [in] elapsed_sec: Elapsed time [sec]
   */
  void CalculateSTM(STMModel stm_model_type, const Orbit* reference_sat_orbit, double mu_m3_s2, double elapsed_sec);
  /**
   * @fn PropagateRK4
   * @brief Propagate relative orbit with RK4
   * @param [in] elapsed_sec: Elapsed time [sec]
   */
  void PropagateRK4(double elapsed_sec);
  /**
   * @fn PropagateSTM
   * @brief Propagate relative orbit with STM
   * @param [in] elapsed_sec: Elapsed time [sec]
   */
  void PropagateSTM(double elapsed_sec);
};

#endif  // S2E_DYNAMICS_ORBIT_RELATIVE_ORBIT_HPP_
