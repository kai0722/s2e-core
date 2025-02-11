/**
 * @file gnss_satellites.cpp
 * @brief Class to calculate GNSS satellite position and related states
 */

#include "gnss_satellites.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

#include "environment/global/physical_constants.hpp"
#include "library/external/sgp4/sgp4ext.h"   //for jday()
#include "library/external/sgp4/sgp4unit.h"  //for gstime()
#include "library/logger/log_utility.hpp"
#include "library/math/constants.hpp"
#include "library/utilities/macros.hpp"

const double nan99 = 999999.999999;

// GNSS satellite number definition
// TODO: Move to other library to define GNSS constants
const int gps_sat_num_ = 32;      //!< Number of GPS satellites
const int glonass_sat_num_ = 26;  //!< Number of GLONASS satellites
const int galileo_sat_num_ = 36;  //!< Number of Galileo satellites
const int beidou_sat_num_ = 16;   //!< Number of BeiDou satellites
const int qzss_sat_num_ = 7;      //!< Number of QZSS satellites TODO: 5 at this moment?

const int gps_index_bias_ = -1;                                          //!< Bias of index for GPS satellites
const int glonass_index_bias_ = gps_index_bias_ + gps_sat_num_;          //!< Bias of index for GLONASS satellites
const int galileo_index_bias_ = glonass_index_bias_ + glonass_sat_num_;  //!< Bias of index for GALILEO satellites
const int beidou_index_bias_ = galileo_index_bias_ + galileo_sat_num_;   //!< Bias of index for BeiDou satellites
const int qzss_index_bias_ = beidou_index_bias_ + beidou_sat_num_;       //!< Bias of index for QZSS satellites

const int all_sat_num_ = gps_sat_num_ + glonass_sat_num_ + galileo_sat_num_ + beidou_sat_num_ + qzss_sat_num_;  //<! Total number of GNSS satellites

using namespace std;

/**
 * @fn initialized_tm
 * @brief Initialize time as calendar expression
 */
tm* initialized_tm() {
  tm* time_tm = (tm*)malloc(sizeof(tm));

  time_tm->tm_year = 0;
  time_tm->tm_mon = 0;
  time_tm->tm_mday = 0;
  time_tm->tm_hour = 0;
  time_tm->tm_min = 0;
  time_tm->tm_sec = 0;

  time_tm->tm_isdst = 0;
  time_tm->tm_yday = 0;
  time_tm->tm_wday = 0;

#ifndef WIN32
  time_tm->tm_zone = NULL;
  time_tm->tm_gmtoff = 0;
#endif

  return time_tm;
}

/**
 * @fn get_unixtime_from_timestamp_line
 * @brief Calculate unix time from calendar expression
 * @param [in] s: Time as calendar expression
 * @return Unix time
 */
double get_unixtime_from_timestamp_line(std::vector<string>& s) {
  tm* time_tm = initialized_tm();
  time_tm->tm_year = stoi(s.at(1)) - 1900;
  time_tm->tm_mon = stoi(s.at(2)) - 1;  // 0 - 11, in time struct, 1 - 12 month is expressed by 1 - 12
  time_tm->tm_mday = stoi(s.at(3));
  time_tm->tm_hour = stoi(s.at(4));
  time_tm->tm_min = stoi(s.at(5));
  time_tm->tm_sec = (int)(stod(s.at(6)) + 1e-4);  // for the numerical error, plus 1e-4 (tm_sec is to be int)
  double unix_time = (double)mktime(time_tm);
  std::free(time_tm);

  return unix_time;
}

// GnssSatelliteBase
template <size_t N>
libra::Vector<N> GnssSatelliteBase::TrigonometricInterpolation(const vector<double>& time_vector, const vector<libra::Vector<N>>& values,
                                                               double time) const {
  size_t n = time_vector.size();
  double w = libra::tau / (24.0 * 60.0 * 60.0) * 1.03;  // coefficient of a day long
  libra::Vector<N> res(0.0);

  for (size_t i = 0; i < n; ++i) {
    double t_k = 1.0;
    for (size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      t_k *= sin(w * (time - time_vector.at(j)) / 2.0) / sin(w * (time_vector.at(i) - time_vector.at(j)) / 2.0);
    }
    for (size_t j = 0; j < (int)N; ++j) {
      res(j) += t_k * values.at(i)(j);
    }
  }

  return res;
}

double GnssSatelliteBase::TrigonometricInterpolation(const vector<double>& time_vector, const vector<double>& values, double time) const {
  size_t n = time_vector.size();
  double w = libra::tau / (24.0 * 60.0 * 60.0) * 1.03;  // coefficient of a day long
  double res = 0.0;

  for (size_t i = 0; i < n; ++i) {
    double t_k = 1.0;
    for (size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      t_k *= sin(w * (time - time_vector.at(j)) / 2.0) / sin(w * (time_vector.at(i) - time_vector.at(j)) / 2.0);
    }
    res += t_k * values.at(i);
  }

  return res;
}

template <size_t N>
libra::Vector<N> GnssSatelliteBase::LagrangeInterpolation(const vector<double>& time_vector, const vector<libra::Vector<N>>& values,
                                                          double time) const {
  int n = time_vector.size();
  libra::Vector<N> res(0.0);

  for (int i = 0; i < n; ++i) {
    double l_i = 1.0;
    for (int j = 0; j < n; ++j) {
      if (i == j) continue;
      l_i *= (time - time_vector.at(j)) / (time_vector.at(i) - time_vector.at(j));
    }
    for (int j = 0; j < N; ++j) {
      res(j) += l_i * values.at(i)(j);
    }
  }

  return res;
}

double GnssSatelliteBase::LagrangeInterpolation(const vector<double>& time_vector, const vector<double>& values, double time) const {
  size_t n = time_vector.size();
  double res = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double l_i = 1.0;
    for (size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      l_i *= (time - time_vector.at(j)) / (time_vector.at(i) - time_vector.at(j));
    }
    res += values.at(i) * l_i;
  }

  return res;
}

int GnssSatelliteBase::GetIndexFromId(string sat_num) const {
  if (sat_num.front() == 'P') {
    switch (sat_num.at(1)) {
      case 'G':
        return stoi(sat_num.substr(2)) + gps_index_bias_;
      case 'R':
        return stoi(sat_num.substr(2)) + glonass_index_bias_;
      case 'E':
        return stoi(sat_num.substr(2)) + galileo_index_bias_;
      case 'C':
        return stoi(sat_num.substr(2)) + beidou_index_bias_;
      case 'J':
        return stoi(sat_num.substr(2)) + qzss_index_bias_;
      default:
        return INT32_MAX;
        break;
    }
  } else {
    switch (sat_num.front()) {
      case 'G':
        return stoi(sat_num.substr(1)) + gps_index_bias_;
      case 'R':
        return stoi(sat_num.substr(1)) + glonass_index_bias_;
      case 'E':
        return stoi(sat_num.substr(1)) + galileo_index_bias_;
      case 'C':
        return stoi(sat_num.substr(1)) + beidou_index_bias_;
      case 'J':
        return stoi(sat_num.substr(1)) + qzss_index_bias_;
      default:
        return INT32_MAX;
        break;
    }
  }
}

string GnssSatelliteBase::GetIdFromIndex(int index) const {
  string res;
  if (index < glonass_index_bias_) {
    res = 'G';
    if (index - gps_index_bias_ < 10) res += '0';
    res += to_string(index - gps_index_bias_);
  } else if (index < galileo_index_bias_) {
    res = 'R';
    if (index - glonass_index_bias_ < 10) res += '0';
    res += to_string(index - glonass_index_bias_);
  } else if (index < beidou_index_bias_) {
    res = 'E';
    if (index - galileo_index_bias_ < 10) res += '0';
    res += to_string(index - galileo_index_bias_);
  } else if (index < qzss_index_bias_) {
    res = 'C';
    if (index - beidou_index_bias_ < 10) res += '0';
    res += to_string(index - beidou_index_bias_);
  } else {
    res = 'J';
    if (index - qzss_index_bias_ < 10) res += '0';
    res += to_string(index - qzss_index_bias_);
  }

  return res;
}

int GnssSatelliteBase::GetNumberOfSatellites() const { return all_sat_num_; }

bool GnssSatelliteBase::GetWhetherValid(int gnss_satellite_id) const {
  if (gnss_satellite_id >= all_sat_num_) return false;
  return validate_.at(gnss_satellite_id);
}

// GnssSatellitePosition
pair<double, double> GnssSatellitePosition::Initialize(vector<vector<string>>& file, int interpolation_method, int interpolation_number,
                                                       UltraRapidMode ur_flag) {
  UNUSED(interpolation_method);

  interpolation_number_ = interpolation_number;

  // Expansion
  time_series_position_ecef_m_.resize(all_sat_num_);  // first vector size is the satellite number
  time_series_position_eci_m_.resize(all_sat_num_);
  unix_time_list.resize(all_sat_num_);

  // for using min and max, set the sup & inf before
  double start_unix_time = 1e16;
  double end_unix_time = 0;

  for (int page = 0; page < (int)file.size(); ++page) {
    // Read Header Info
    int num_of_time_stamps = 0;
    int num_of_sat = 0;
    int line;
    for (line = 0; line < 3; ++line) {
      istringstream iss{file.at(page).at(line)};

      if (line == 0) {
        // in seventh line, there is time stamps
        // http://epncb.oma.be/ftp/data/format/sp3c.txt
        for (int i = 0; i < 7; ++i) {
          // how many time stamps are written?
          string each;
          iss >> each;
          if (i == 6) num_of_time_stamps = stoi(each);
        }
      } else if (line == 1) {
        for (int i = 0; i < 4; ++i) {
          string each;
          iss >> each;
          if (i == 3) time_interval_ = stod(each);
        }
      } else if (line == 2) {
        for (int i = 0; i < 2; ++i) {
          string each;
          iss >> each;
          if (i == 1) num_of_sat = stoi(each);
        }
      }
    }
    line = 3;
    while (file.at(page).at(line).front() != '*') ++line;

    // Calculate number of data lines
    int start_line, end_line;
    if (ur_flag == kNotUse) {
      start_line = line;
      end_line = line + (num_of_sat + 1) * num_of_time_stamps;
    } else {
      int offset = (int)ur_flag - (int)kObserve1;
      start_line = line + (num_of_sat + 1) * num_of_time_stamps / 8 * offset;
      end_line = line + (num_of_sat + 1) * num_of_time_stamps / 8 * (offset + 1);
    }

    // Read time and position data
    double unix_time = 0;
    double cos_ = 0.0;  //!< cos value for ECEF->ECI conversion
    double sin_ = 0.0;  //!< sin value for ECEF->ECI conversion
    for (int i = 0; i < end_line - start_line; ++i) {
      line = i + start_line;

      istringstream iss{file.at(page).at(line)};
      vector<string> s;
      if (i % (num_of_sat + 1) == 0) {
        // Epoch information
        for (int j = 0; j < 7; ++j) {
          string tmp;
          iss >> tmp;
          s.push_back(tmp);
        }
        // Convert to julian date
        unix_time = get_unixtime_from_timestamp_line(s);
        double jd;
        jday(stoi(s.at(1)), stoi(s.at(2)), stoi(s.at(3)), stoi(s.at(4)), stoi(s.at(5)), stod(s.at(6)), jd);
        // Calculate frame conversion
        double gs_time_ = gstime(jd);
        cos_ = cos(gs_time_);
        sin_ = sin(gs_time_);
        // Set start and end unix time
        start_unix_time = std::min(start_unix_time, unix_time);
        end_unix_time = std::max(end_unix_time, unix_time);
      } else {
        // Position and clock data of each GNSS satellite
        for (int j = 0; j < 5; ++j) {
          string tmp;
          iss >> tmp;
          s.push_back(tmp);
        }
        int gnss_satellite_id = GetIndexFromId(s.front());

        bool available_flag = true;
        libra::Vector<3> ecef_position_m(0.0);
        for (int j = 0; j < 3; ++j) {
          if (std::abs(stod(s.at(j + 1)) - nan99) < 1.0) {
            available_flag = false;
            break;
          } else {
            ecef_position_m(j) = stod(s.at(j + 1));
          }
        }
        if (!available_flag) continue;

        // [km] -> [m]
        ecef_position_m *= 1000.0;

        // ECI frame conversion
        libra::Vector<3> eci_position(0.0);
        double x = ecef_position_m(0);
        double y = ecef_position_m(1);
        double z = ecef_position_m(2);
        eci_position(0) = cos_ * x - sin_ * y;
        eci_position(1) = sin_ * x + cos_ * y;
        eci_position(2) = z;

        // Set data
        if (!unix_time_list.at(gnss_satellite_id).empty() && std::abs(unix_time - unix_time_list.at(gnss_satellite_id).back()) < 1.0) {
          unix_time_list.at(gnss_satellite_id).back() = unix_time;
          time_series_position_ecef_m_.at(gnss_satellite_id).back() = ecef_position_m;
          time_series_position_eci_m_.at(gnss_satellite_id).back() = eci_position;
        } else {
          unix_time_list.at(gnss_satellite_id).emplace_back(unix_time);
          time_series_position_ecef_m_.at(gnss_satellite_id).emplace_back(ecef_position_m);
          time_series_position_eci_m_.at(gnss_satellite_id).emplace_back(eci_position);
        }
      }
    }
  }

  return make_pair(start_unix_time, end_unix_time);
}

void GnssSatellitePosition::SetUp(const double start_unix_time, const double step_width_s) {
  step_width_s_ = step_width_s;

  position_ecef_m_.assign(all_sat_num_, libra::Vector<3>(0.0));
  position_eci_m_.assign(all_sat_num_, libra::Vector<3>(0.0));
  validate_.assign(all_sat_num_, false);

  nearest_index_.resize(all_sat_num_);
  time_period_list_.resize(all_sat_num_);

  ecef_.resize(all_sat_num_);
  eci_.resize(all_sat_num_);

  for (int gnss_satellite_id = 0; gnss_satellite_id < all_sat_num_; ++gnss_satellite_id) {
    if (unix_time_list.at(gnss_satellite_id).empty()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    int index = (int)(lower_bound(unix_time_list.at(gnss_satellite_id).begin(), unix_time_list.at(gnss_satellite_id).end(), start_unix_time) -
                      unix_time_list.at(gnss_satellite_id).begin());
    if (index == (int)unix_time_list.at(gnss_satellite_id).size()) {
      nearest_index_.at(gnss_satellite_id) = index;
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    double nearest_unixtime = unix_time_list.at(gnss_satellite_id).at(index);
    if (interpolation_number_ % 2 && index != 0) {
      double pre_time = unix_time_list.at(gnss_satellite_id).at(index - 1);
      if (std::abs(start_unix_time - pre_time) < std::abs(start_unix_time - nearest_unixtime)) --index;
    }
    nearest_index_.at(gnss_satellite_id) = index;
    nearest_unixtime = unix_time_list.at(gnss_satellite_id).at(index);
    if (std::abs(start_unix_time - nearest_unixtime) > time_interval_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    // for both even and odd: 2n+1 -> [-n, n] 2n -> [-n, n)
    for (int j = -interpolation_number_ / 2; j < (interpolation_number_ + 1) / 2; ++j) {
      int now_index = index + j;
      if (now_index < 0 || now_index >= (int)unix_time_list.at(gnss_satellite_id).size()) continue;

      time_period_list_.at(gnss_satellite_id).push_back(unix_time_list.at(gnss_satellite_id).at(now_index));
      ecef_.at(gnss_satellite_id).push_back(time_series_position_ecef_m_.at(gnss_satellite_id).at(now_index));
      eci_.at(gnss_satellite_id).push_back(time_series_position_eci_m_.at(gnss_satellite_id).at(now_index));
    }
    if ((int)time_period_list_.at(gnss_satellite_id).size() != interpolation_number_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    double time_period_length = time_period_list_.at(gnss_satellite_id).back() - time_period_list_.at(gnss_satellite_id).front();
    if (time_period_length > time_interval_ * (interpolation_number_ - 1 + 3) + 1e-4) {  // allow for 3 missing
      validate_.at(gnss_satellite_id) = false;
      continue;
    } else {
      validate_.at(gnss_satellite_id) = true;
    }

    if (std::abs(start_unix_time - nearest_unixtime) < 1e-4) {  // for the numerical error, plus 1e-4
      position_ecef_m_.at(gnss_satellite_id) = time_series_position_ecef_m_.at(gnss_satellite_id).at(index);
      position_eci_m_.at(gnss_satellite_id) = time_series_position_eci_m_.at(gnss_satellite_id).at(index);
    } else {
      position_ecef_m_.at(gnss_satellite_id) =
          TrigonometricInterpolation(time_period_list_.at(gnss_satellite_id), ecef_.at(gnss_satellite_id), start_unix_time);
      position_eci_m_.at(gnss_satellite_id) =
          TrigonometricInterpolation(time_period_list_.at(gnss_satellite_id), eci_.at(gnss_satellite_id), start_unix_time);
    }
  }
}

void GnssSatellitePosition::Update(const double current_unix_time) {
  for (int gnss_satellite_id = 0; gnss_satellite_id < all_sat_num_; ++gnss_satellite_id) {
    if (unix_time_list.at(gnss_satellite_id).empty()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    int index = nearest_index_.at(gnss_satellite_id);
    if (index == (int)unix_time_list.at(gnss_satellite_id).size()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    if (index + 1 < (int)unix_time_list.at(gnss_satellite_id).size()) {
      double pre_unix = unix_time_list.at(gnss_satellite_id).at(index);
      double post_unix = unix_time_list.at(gnss_satellite_id).at(index + 1);

      if (std::abs(current_unix_time - post_unix) < std::abs(current_unix_time - pre_unix)) {
        ++index;
        nearest_index_.at(gnss_satellite_id) = index;

        time_period_list_.at(gnss_satellite_id).clear();
        ecef_.at(gnss_satellite_id).clear();
        eci_.at(gnss_satellite_id).clear();

        // for both even and odd: 2n+1 -> [-n, n] 2n -> [-n, n)
        for (int j = -interpolation_number_ / 2; j < (interpolation_number_ + 1) / 2; ++j) {
          int now_index = index + j;
          if (now_index < 0 || now_index >= (int)unix_time_list.at(gnss_satellite_id).size()) continue;

          time_period_list_.at(gnss_satellite_id).push_back(unix_time_list.at(gnss_satellite_id).at(now_index));
          ecef_.at(gnss_satellite_id).push_back(time_series_position_ecef_m_.at(gnss_satellite_id).at(now_index));
          eci_.at(gnss_satellite_id).push_back(time_series_position_eci_m_.at(gnss_satellite_id).at(now_index));
        }
      }
    }
    double nearest_unix_time = unix_time_list.at(gnss_satellite_id).at(index);
    if (std::abs(current_unix_time - nearest_unix_time) > time_interval_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    if ((int)time_period_list_.at(gnss_satellite_id).size() != interpolation_number_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    double time_period_length = time_period_list_.at(gnss_satellite_id).back() - time_period_list_.at(gnss_satellite_id).front();
    if (time_period_length > time_interval_ * (interpolation_number_ - 1 + 3) + 1e-4) {  // allow for 3 missing
      validate_.at(gnss_satellite_id) = false;
      continue;
    } else {
      validate_.at(gnss_satellite_id) = true;
    }

    if (std::abs(current_unix_time - nearest_unix_time) < 1e-4) {  // for the numerical error, plus 1e-4
      position_ecef_m_.at(gnss_satellite_id) = time_series_position_ecef_m_.at(gnss_satellite_id).at(index);
      position_eci_m_.at(gnss_satellite_id) = time_series_position_eci_m_.at(gnss_satellite_id).at(index);
    } else {
      position_ecef_m_.at(gnss_satellite_id) =
          TrigonometricInterpolation(time_period_list_.at(gnss_satellite_id), ecef_.at(gnss_satellite_id), current_unix_time);
      position_eci_m_.at(gnss_satellite_id) =
          TrigonometricInterpolation(time_period_list_.at(gnss_satellite_id), eci_.at(gnss_satellite_id), current_unix_time);
    }
  }
}

libra::Vector<3> GnssSatellitePosition::GetPosition_ecef_m(int gnss_satellite_id) const {
  if (gnss_satellite_id >= all_sat_num_) return libra::Vector<3>(0.0);
  return position_ecef_m_.at(gnss_satellite_id);
}

libra::Vector<3> GnssSatellitePosition::GetPosition_eci_m(int gnss_satellite_id) const {
  if (gnss_satellite_id >= all_sat_num_) return libra::Vector<3>(0.0);
  return position_eci_m_.at(gnss_satellite_id);
}

// GnssSatelliteClock
void GnssSatelliteClock::Initialize(vector<vector<string>>& file, string file_extension, int interpolation_number, UltraRapidMode ur_flag,
                                    pair<double, double> unix_time_period) {
  interpolation_number_ = interpolation_number;
  time_series_clock_offset_m_.resize(all_sat_num_);  // first vector size is the sat num
  unix_time_list.resize(all_sat_num_);

  if (file_extension == ".sp3") {
    for (int page = 0; page < (int)file.size(); ++page) {
      // Read Header Info
      int num_of_time_stamps = 0;
      int num_of_sat = 0;
      int line;
      for (line = 0; line < 3; ++line) {
        istringstream iss{file.at(page).at(line)};

        if (line == 0) {
          // in seventh line, there is time stamps
          // http://epncb.oma.be/ftp/data/format/sp3c.txt
          for (int i = 0; i < 7; ++i) {
            // how many time stamps are written?
            string each;
            iss >> each;
            if (i == 6) num_of_time_stamps = stoi(each);
          }
        } else if (line == 1) {
          for (int i = 0; i < 4; ++i) {
            string each;
            iss >> each;
            if (i == 3) time_interval_ = stod(each);
          }
        } else if (line == 2) {
          for (int i = 0; i < 2; ++i) {
            string each;
            iss >> each;
            if (i == 1) num_of_sat = stoi(each);
          }
        }
      }
      line = 3;
      while (file.at(page).at(line).front() != '*') ++line;

      // Calculate number of data lines
      int start_line, end_line;
      if (ur_flag == kNotUse) {
        start_line = line;
        end_line = line + (num_of_sat + 1) * num_of_time_stamps;
      } else {
        int offset = (int)ur_flag - (int)kObserve1;
        start_line = line + (num_of_sat + 1) * num_of_time_stamps / 8 * offset;
        end_line = line + (num_of_sat + 1) * num_of_time_stamps / 8 * (offset + 1);
      }

      // Read time and clock data
      double unix_time = 0;
      for (int i = 0; i < end_line - start_line; ++i) {
        line = i + start_line;

        istringstream iss{file.at(page).at(line)};
        vector<string> s;
        if (i % (num_of_sat + 1) == 0) {
          // Epoch information
          for (int j = 0; j < 7; ++j) {
            string tmp;
            iss >> tmp;
            s.push_back(tmp);
          }
          unix_time = get_unixtime_from_timestamp_line(s);
        } else {
          for (int j = 0; j < 5; ++j) {
            string tmp;
            iss >> tmp;
            s.push_back(tmp);
          }
          int gnss_satellite_id = GetIndexFromId(s.front());

          double clock = stod(s.at(4));
          if (std::abs(clock - nan99) < 1.0) continue;

          // In the file, clock bias is expressed in [micro second], so by multiplying by the speed_of_light & 1e-6, they are converted to [m]
          clock *= (environment::speed_of_light_m_s * 1e-6);
          if (!unix_time_list.at(gnss_satellite_id).empty() && std::abs(unix_time - unix_time_list.at(gnss_satellite_id).back()) < 1.0) {
            unix_time_list.at(gnss_satellite_id).back() = unix_time;
            time_series_clock_offset_m_.at(gnss_satellite_id).back() = clock;
          } else {
            unix_time_list.at(gnss_satellite_id).push_back(unix_time);
            time_series_clock_offset_m_.at(gnss_satellite_id).emplace_back(clock);
          }
        }
      }
    }
  } else {  // .clk30s
    if (kPredict1 <= ur_flag && ur_flag <= kPredict4) {
      cout << "clock settings has something wrong" << endl;
      exit(1);
    }
    time_interval_ = 1e9;

    for (int page = 0; page < (int)file.size(); ++page) {
      double start_unix_time, end_unix_time;
      if (ur_flag == kNotUse) {
        start_unix_time = unix_time_period.first;
        end_unix_time = unix_time_period.second + 30;
      } else {
        start_unix_time = -1;
        end_unix_time = 0;
      }
      for (int line = 0; line < (int)file.at(page).size(); ++line) {
        if (file.at(page).at(line).substr(0, 3) != "AS ") continue;

        istringstream iss{file.at(page).at(line)};
        vector<string> s;
        for (int i = 0; i < 11; ++i) {
          string tmp;
          iss >> tmp;
          s.push_back(tmp);
        }

        tm* time_tm = initialized_tm();
        time_tm->tm_year = stoi(s.at(2)) - 1900;
        time_tm->tm_mon = stoi(s.at(3)) - 1;  // 0 - 11, in time struct, 1 - 12 month is expressed by 1 - 12
        time_tm->tm_mday = stoi(s.at(4));
        time_tm->tm_hour = stoi(s.at(5));
        time_tm->tm_min = stoi(s.at(6));
        time_tm->tm_sec = (int)(stod(s.at(7)) + 1e-4);  // for the numerical error, plus 1e-4. tm_sec is to be int
        double unix_time = (double)mktime(time_tm);
        const double interval = 6 * 60 * 60;
        if (start_unix_time < 0) {
          start_unix_time = unix_time + (ur_flag - kObserve1) * interval;  // Fix here to use enum class
          end_unix_time = start_unix_time + interval;
        }

        std::free(time_tm);

        int gnss_satellite_id = GetIndexFromId(s.at(1));
        double clock_bias = stod(s.at(9)) * environment::speed_of_light_m_s;  // [s] -> [m]
        if (start_unix_time - unix_time > 1e-4) continue;                     // for the numerical error
        if (end_unix_time - unix_time < 1e-4) break;
        if (!unix_time_list.at(gnss_satellite_id).empty() &&
            std::abs(unix_time - unix_time_list.at(gnss_satellite_id).back()) < 1e-4) {  // for the numerical error
          unix_time_list.at(gnss_satellite_id).back() = unix_time;
          time_series_clock_offset_m_.at(gnss_satellite_id).back() = clock_bias;
        } else {
          if (!unix_time_list.at(gnss_satellite_id).empty())
            time_interval_ = min(time_interval_, unix_time - unix_time_list.at(gnss_satellite_id).back());
          unix_time_list.at(gnss_satellite_id).emplace_back(unix_time);
          time_series_clock_offset_m_.at(gnss_satellite_id).emplace_back(clock_bias);
        }
      }
    }
  }
}

void GnssSatelliteClock::SetUp(const double start_unix_time, const double step_width_s) {
  step_width_s_ = step_width_s;

  clock_offset_m_.resize(all_sat_num_);
  validate_.assign(all_sat_num_, false);

  nearest_index_.resize(all_sat_num_);
  time_period_list_.resize(all_sat_num_);

  clock_bias_.resize(all_sat_num_);

  for (int gnss_satellite_id = 0; gnss_satellite_id < all_sat_num_; ++gnss_satellite_id) {
    if (unix_time_list.at(gnss_satellite_id).empty()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    int index = (int)(lower_bound(unix_time_list.at(gnss_satellite_id).begin(), unix_time_list.at(gnss_satellite_id).end(), start_unix_time) -
                      unix_time_list.at(gnss_satellite_id).begin());
    if (index == (int)unix_time_list.at(gnss_satellite_id).size()) {
      validate_.at(gnss_satellite_id) = false;
      nearest_index_.at(gnss_satellite_id) = index;
      continue;
    }

    double nearest_unixtime = unix_time_list.at(gnss_satellite_id).at(index);
    if (interpolation_number_ % 2 && index != 0) {
      double pre_time = unix_time_list.at(gnss_satellite_id).at(index - 1);
      if (std::abs(start_unix_time - pre_time) < std::abs(start_unix_time - nearest_unixtime)) --index;
    }
    nearest_index_.at(gnss_satellite_id) = index;
    nearest_unixtime = unix_time_list.at(gnss_satellite_id).at(index);
    if (std::abs(start_unix_time - nearest_unixtime) > time_interval_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    // for both even and odd: 2n+1 -> [-n, n] 2n -> [-n, n)
    for (int j = -interpolation_number_ / 2; j < (interpolation_number_ + 1) / 2; ++j) {
      int now_index = index + j;
      if (now_index < 0 || now_index >= (int)unix_time_list.at(gnss_satellite_id).size()) continue;

      time_period_list_.at(gnss_satellite_id).push_back(unix_time_list.at(gnss_satellite_id).at(now_index));
      clock_bias_.at(gnss_satellite_id).push_back(time_series_clock_offset_m_.at(gnss_satellite_id).at(now_index));
    }

    if ((int)time_period_list_.at(gnss_satellite_id).size() != interpolation_number_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }
    double time_period_length = time_period_list_.at(gnss_satellite_id).back() - time_period_list_.at(gnss_satellite_id).front();
    if (time_period_length > time_interval_ * (interpolation_number_ - 1) + 1e-4) {  // more strict for clock_bias
      validate_.at(gnss_satellite_id) = false;
      continue;
    } else {
      validate_.at(gnss_satellite_id) = true;
    }

    if (std::abs(start_unix_time - nearest_unixtime) < 1e-4) {  // for the numerical error
      clock_offset_m_.at(gnss_satellite_id) = time_series_clock_offset_m_.at(gnss_satellite_id).at(index);
    } else {
      clock_offset_m_.at(gnss_satellite_id) =
          LagrangeInterpolation(time_period_list_.at(gnss_satellite_id), clock_bias_.at(gnss_satellite_id), start_unix_time);
    }
  }
}

void GnssSatelliteClock::Update(const double current_unix_time) {
  for (int gnss_satellite_id = 0; gnss_satellite_id < all_sat_num_; ++gnss_satellite_id) {
    if (unix_time_list.at(gnss_satellite_id).empty()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    int index = nearest_index_.at(gnss_satellite_id);
    if (index == (int)unix_time_list.at(gnss_satellite_id).size()) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    if (index + 1 < (int)unix_time_list.at(gnss_satellite_id).size()) {
      double pre_unix = unix_time_list.at(gnss_satellite_id).at(index);
      double post_unix = unix_time_list.at(gnss_satellite_id).at(index + 1);

      if (std::abs(current_unix_time - post_unix) < std::abs(current_unix_time - pre_unix)) {
        ++index;
        nearest_index_.at(gnss_satellite_id) = index;

        time_period_list_.at(gnss_satellite_id).clear();
        clock_bias_.at(gnss_satellite_id).clear();

        // for both even and odd: 2n+1 -> [-n, n] 2n -> [-n, n)
        for (int j = -interpolation_number_ / 2; j < (interpolation_number_ + 1) / 2; ++j) {
          int now_index = index + j;
          if (now_index < 0 || now_index >= (int)unix_time_list.at(gnss_satellite_id).size()) continue;

          time_period_list_.at(gnss_satellite_id).push_back(unix_time_list.at(gnss_satellite_id).at(now_index));
          clock_bias_.at(gnss_satellite_id).push_back(time_series_clock_offset_m_.at(gnss_satellite_id).at(now_index));
        }
      }
    }
    if ((int)time_period_list_.at(gnss_satellite_id).size() != interpolation_number_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    double nearest_unix_time = unix_time_list.at(gnss_satellite_id).at(index);
    if (std::abs(current_unix_time - nearest_unix_time) > time_interval_) {
      validate_.at(gnss_satellite_id) = false;
      continue;
    }

    // in clock_bias, more strict.
    double time_period_length = time_period_list_.at(gnss_satellite_id).back() - time_period_list_.at(gnss_satellite_id).front();
    if (time_period_length > time_interval_ * (interpolation_number_ - 1) + 1e-4) {  // more strict for clock_bias
      validate_.at(gnss_satellite_id) = false;
      continue;
    } else {
      validate_.at(gnss_satellite_id) = true;
    }

    if (std::abs(current_unix_time - nearest_unix_time) < 1e-4) {
      clock_offset_m_.at(gnss_satellite_id) = time_series_clock_offset_m_.at(gnss_satellite_id).at(index);
    } else {
      clock_offset_m_.at(gnss_satellite_id) =
          LagrangeInterpolation(time_period_list_.at(gnss_satellite_id), clock_bias_.at(gnss_satellite_id), current_unix_time);
    }
  }
}

double GnssSatelliteClock::GetSatClock(int gnss_satellite_id) const {
  if (gnss_satellite_id >= all_sat_num_) return 0.0;
  return clock_offset_m_.at(gnss_satellite_id);
}

// GnssSatelliteInformation
GnssSatelliteInformation::GnssSatelliteInformation() {}
void GnssSatelliteInformation::Initialize(vector<vector<string>>& position_file, int position_interpolation_method, int position_interpolation_number,
                                          UltraRapidMode position_ur_flag, vector<vector<string>>& clock_file, string clock_file_extension,
                                          int clock_interpolation_number, UltraRapidMode clock_ur_flag) {
  auto unix_time_period = position_.Initialize(position_file, position_interpolation_method, position_interpolation_number, position_ur_flag);
  clock_.Initialize(clock_file, clock_file_extension, clock_interpolation_number, clock_ur_flag, unix_time_period);
}

void GnssSatelliteInformation::SetUp(const double start_unix_time, const double step_width_s) {
  position_.SetUp(start_unix_time, step_width_s);
  clock_.SetUp(start_unix_time, step_width_s);
}

void GnssSatelliteInformation::Update(const double current_unix_time) {
  position_.Update(current_unix_time);
  clock_.Update(current_unix_time);
}

int GnssSatelliteInformation::GetNumberOfSatellites() const {
  if (position_.GetNumberOfSatellites() == clock_.GetNumberOfSatellites()) {
    return position_.GetNumberOfSatellites();
  } else {
    cout << "Num Of Gnss Satellites has something wrong" << endl;
    return 0;
  }
}

bool GnssSatelliteInformation::GetWhetherValid(int gnss_satellite_id) const {
  if (position_.GetWhetherValid(gnss_satellite_id) && clock_.GetWhetherValid(gnss_satellite_id)) return true;
  return false;
}

libra::Vector<3> GnssSatelliteInformation::GetSatellitePositionEcef(int gnss_satellite_id) const {
  return position_.GetPosition_ecef_m(gnss_satellite_id);
}

libra::Vector<3> GnssSatelliteInformation::GetSatellitePositionEci(int gnss_satellite_id) const {
  return position_.GetPosition_eci_m(gnss_satellite_id);
}

double GnssSatelliteInformation::GetSatelliteClock(int gnss_satellite_id) const { return clock_.GetSatClock(gnss_satellite_id); }

// GnssSatellites
GnssSatellites::GnssSatellites(bool is_calc_enabled)
#ifdef GNSS_SATELLITES_DEBUG_OUTPUT
    : ofs_true("true.csv"),
      ofs_estimation("estimation.csv"),
      ofs_sa("sa.csv")
#endif
{
  // TODO: Add log enable flag in ini file
  is_calc_enabled_ = is_calc_enabled;
  if (is_calc_enabled_) {
    is_log_enabled_ = true;
  } else {
    is_log_enabled_ = false;
  }
}

bool GnssSatellites::IsCalcEnabled() const { return is_calc_enabled_; }

void GnssSatellites::Initialize(vector<vector<string>>& true_position_file, int true_position_interpolation_method,
                                int true_position_interpolation_number, UltraRapidMode true_position_ur_flag,

                                vector<vector<string>>& true_clock_file, string true_clock_file_extension, int true_clock_interpolation_number,
                                UltraRapidMode true_clock_ur_flag,

                                vector<vector<string>>& estimate_position_file, int estimate_position_interpolation_method,
                                int estimate_position_interpolation_number, UltraRapidMode estimate_position_ur_flag,

                                vector<vector<string>>& estimate_clock_file, string estimate_clock_file_extension,
                                int estimate_clock_interpolation_number, UltraRapidMode estimate_clock_ur_flag) {
  true_info_.Initialize(true_position_file, true_position_interpolation_method, true_position_interpolation_number, true_position_ur_flag,

                        true_clock_file, true_clock_file_extension, true_clock_interpolation_number, true_clock_ur_flag);

  estimate_info_.Initialize(estimate_position_file, estimate_position_interpolation_method, estimate_position_interpolation_number,
                            estimate_position_ur_flag,

                            estimate_clock_file, estimate_clock_file_extension, estimate_clock_interpolation_number, estimate_clock_ur_flag);

  return;
}

void GnssSatellites::SetUp(const SimulationTime* simulation_time) {
  if (!IsCalcEnabled()) return;

  tm* start_tm = initialized_tm();
  start_tm->tm_year = simulation_time->GetStartYear() - 1900;
  start_tm->tm_mon = simulation_time->GetStartMonth() - 1;
  start_tm->tm_mday = simulation_time->GetStartDay();
  start_tm->tm_hour = simulation_time->GetStartHour();
  start_tm->tm_min = simulation_time->GetStartMinute();
  double start_sec = simulation_time->GetStartSecond();
  start_tm->tm_sec = (int)start_sec;
  double unix_time = (double)mktime(start_tm) + start_sec - floor(start_sec);
  std::free(start_tm);
  true_info_.SetUp(unix_time, simulation_time->GetSimulationStep_s());
  estimate_info_.SetUp(unix_time, simulation_time->GetSimulationStep_s());

  start_unix_time_ = unix_time;

  return;
}

void GnssSatellites::Update(const SimulationTime* simulation_time) {
  if (!IsCalcEnabled()) return;

  double elapsed_sec = simulation_time->GetElapsedTime_s();

  true_info_.Update(elapsed_sec + start_unix_time_);
  estimate_info_.Update(elapsed_sec + start_unix_time_);

#ifdef GNSS_SATELLITES_DEBUG_OUTPUT
  DebugOutput();
#endif

  return;
}

int GnssSatellites::GetNumberOfSatellites() const { return estimate_info_.GetNumberOfSatellites(); }

string GnssSatellites::GetIdFromIndex(int index) const { return estimate_info_.GetGnssSatPos().GetIdFromIndex(index); }

int GnssSatellites::GetIndexFromId(string sat_num) const { return estimate_info_.GetGnssSatPos().GetIndexFromId(sat_num); }

bool GnssSatellites::GetWhetherValid(int gnss_satellite_id) const {
  if (gnss_satellite_id >= GetNumberOfSatellites()) return false;

  if (true_info_.GetWhetherValid(gnss_satellite_id) && estimate_info_.GetWhetherValid(gnss_satellite_id))
    return true;
  else
    return false;
}

libra::Vector<3> GnssSatellites::GetSatellitePositionEcef(const int gnss_satellite_id) const {
  // gnss_satellite_id is wrong or not valid
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) {
    libra::Vector<3> res(0);
    return res;
  }

  return estimate_info_.GetSatellitePositionEcef(gnss_satellite_id);
}

libra::Vector<3> GnssSatellites::GetSatellitePositionEci(const int gnss_satellite_id) const {
  // gnss_satellite_id is wrong or not valid
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) {
    libra::Vector<3> res(0);
    return res;
  }

  return estimate_info_.GetSatellitePositionEci(gnss_satellite_id);
}

double GnssSatellites::GetSatelliteClock(const int gnss_satellite_id) const {
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) {
    return 0.0;
  }

  return estimate_info_.GetSatelliteClock(gnss_satellite_id);
}

double GnssSatellites::GetPseudoRangeECEF(const int gnss_satellite_id, libra::Vector<3> rec_position, double rec_clock,
                                          const double frequency) const {
  // gnss_satellite_id is wrong or not validate
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) return 0.0;

  double res = 0.0;
  auto gnss_position = true_info_.GetSatellitePositionEcef(gnss_satellite_id);
  for (int i = 0; i < 3; ++i) {
    res += pow(rec_position(i) - gnss_position(i), 2.0);
  }
  res = sqrt(res);

  // clock bias
  res += rec_clock - true_info_.GetSatelliteClock(gnss_satellite_id);

  // ionospheric delay
  const double ionospheric_delay = AddIonosphericDelay(gnss_satellite_id, rec_position, frequency, GnssFrameDefinition::kEcef);

  res += ionospheric_delay;

  return res;
}

double GnssSatellites::GetPseudoRangeECI(const int gnss_satellite_id, libra::Vector<3> rec_position, double rec_clock, const double frequency) const {
  // gnss_satellite_id is wrong or not validate
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) return 0.0;

  double res = 0.0;
  auto gnss_position = true_info_.GetSatellitePositionEci(gnss_satellite_id);
  for (int i = 0; i < 3; ++i) {
    res += pow(rec_position(i) - gnss_position(i), 2.0);
  }
  res = sqrt(res);

  // clock bias
  res += rec_clock - true_info_.GetSatelliteClock(gnss_satellite_id);

  // ionospheric delay
  const double ionospheric_delay = AddIonosphericDelay(gnss_satellite_id, rec_position, frequency, GnssFrameDefinition::kEci);

  res += ionospheric_delay;

  return res;
}

pair<double, double> GnssSatellites::GetCarrierPhaseECEF(const int gnss_satellite_id, libra::Vector<3> rec_position, double rec_clock,
                                                         const double frequency) const {
  // gnss_satellite_id is wrong or not validate
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) return {0.0, 0.0};

  double res = 0.0;
  auto gnss_position = true_info_.GetSatellitePositionEcef(gnss_satellite_id);
  for (int i = 0; i < 3; ++i) {
    res += pow(rec_position(i) - gnss_position(i), 2.0);
  }
  res = sqrt(res);

  // clock bias
  res += rec_clock - true_info_.GetSatelliteClock(gnss_satellite_id);

  // ionospheric delay
  const double ionospheric_delay = AddIonosphericDelay(gnss_satellite_id, rec_position, frequency, GnssFrameDefinition::kEcef);

  res -= ionospheric_delay;

  // wavelength frequency is thought to be given by MHz
  double lambda = environment::speed_of_light_m_s * 1e-6 / frequency;
  double cycle = res / lambda;

  double bias = floor(cycle);
  cycle -= bias;

  return {cycle, bias};
}

pair<double, double> GnssSatellites::GetCarrierPhaseECI(const int gnss_satellite_id, libra::Vector<3> rec_position, double rec_clock,
                                                        const double frequency) const {
  // gnss_satellite_id is wrong or not validate
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) return {0.0, 0.0};

  double res = 0.0;
  auto gnss_position = true_info_.GetSatellitePositionEci(gnss_satellite_id);
  for (int i = 0; i < 3; ++i) {
    res += pow(rec_position(i) - gnss_position(i), 2.0);
  }
  res = sqrt(res);

  // clock bias
  res += rec_clock - true_info_.GetSatelliteClock(gnss_satellite_id);

  // ionospheric delay
  const double ionospheric_delay = AddIonosphericDelay(gnss_satellite_id, rec_position, frequency, GnssFrameDefinition::kEci);

  res -= ionospheric_delay;

  // wavelength frequency is thought to be given by MHz
  double lambda = environment::speed_of_light_m_s * 1e-6 / frequency;
  double cycle = res / lambda;

  double bias = floor(cycle);
  cycle -= bias;

  return {cycle, bias};
}

// for Ionospheric delay I[m]
double GnssSatellites::AddIonosphericDelay(const int gnss_satellite_id, const libra::Vector<3> rec_position, const double frequency,
                                           const GnssFrameDefinition flag) const {
  // gnss_satellite_id is wrong or not validate
  if (gnss_satellite_id >= GetNumberOfSatellites() || !GetWhetherValid(gnss_satellite_id)) return 0.0;

  const double earth_hemisphere_km = environment::earth_equatorial_radius_m / 1000.0;

  double altitude = 0.0;
  for (int i = 0; i < 3; ++i) altitude += pow(rec_position[i], 2.0);
  altitude = sqrt(altitude);
  altitude = altitude / 1000.0 - earth_hemisphere_km;  //[m -> km]
  if (altitude >= 1000.0) return 0.0;                  // there is no Ionosphere above 1000km

  libra::Vector<3> gnss_position;
  if (flag == GnssFrameDefinition::kEcef)
    gnss_position = true_info_.GetSatellitePositionEcef(gnss_satellite_id);
  else if (flag == GnssFrameDefinition::kEci)
    gnss_position = true_info_.GetSatellitePositionEci(gnss_satellite_id);

  double angle_rad = CalcAngleTwoVectors_rad(rec_position, gnss_position - rec_position);
  const double default_delay = 20.0;  //[m] default delay
  // Assume the maximum height as 1000.0. Divide by cos because the slope makes it longer.
  double delay = default_delay * (1000.0 - altitude) / 1000.0 / cos(angle_rad);
  const double default_frequency = 1500.0;  //[MHz]
  // Ionospheric delay is inversely proportional to the square of the frequency
  delay *= pow(default_frequency / frequency, 2.0);

  return delay;
}

std::string GnssSatellites::GetLogHeader() const {
  std::string str_tmp = "";

  // TODO: Add log output for other navigation systems
  for (size_t gps_index = 0; gps_index < gps_sat_num_; gps_index++) {
    str_tmp += WriteVector("GPS" + std::to_string(gps_index) + "_position", "ecef", "m", 3);
    str_tmp += WriteScalar("GPS" + std::to_string(gps_index) + "_clock_offset", "m");
  }

  return str_tmp;
}

std::string GnssSatellites::GetLogValue() const {
  std::string str_tmp = "";

  for (size_t gps_index = 0; gps_index < gps_sat_num_; gps_index++) {
    str_tmp += WriteVector(true_info_.GetSatellitePositionEcef((int)gps_index), 16);
    str_tmp += WriteScalar(true_info_.GetSatelliteClock((int)gps_index));
  }

  return str_tmp;
}

void GnssSatellites::DebugOutput() {
#ifdef GNSS_SATELLITES_DEBUG_OUTPUT
  for (int gnss_satellite_id = 0; gnss_satellite_id < gps_sat_num_; ++gnss_satellite_id) {
    if (true_info_.GetWhetherValid(gnss_satellite_id)) {
      auto true_pos = true_info_.GetSatellitePositionEcef(gnss_satellite_id);
      for (int i = 0; i < 3; ++i) {
        ofs_true << fixed << setprecision(10) << true_pos[i] << ",";
      }
      auto true_clock = true_info_.GetSatelliteClock(gnss_satellite_id);
      ofs_true << true_clock << ",";
    } else {
      for (int i = 0; i < 4; ++i) {
        ofs_true << 0.0 << ",";
      }
    }

    if (estimate_info_.GetWhetherValid(gnss_satellite_id)) {
      auto estimation_pos = estimate_info_.GetSatellitePositionEcef(gnss_satellite_id);
      for (int i = 0; i < 3; ++i) {
        ofs_estimation << fixed << setprecision(10) << estimation_pos[i] << ",";
      }
      auto estimation_clock = estimate_info_.GetSatelliteClock(gnss_satellite_id);
      ofs_estimation << estimation_clock << ",";
    } else {
      for (int i = 0; i < 4; ++i) {
        ofs_estimation << 0.0 << ",";
      }
    }

    if (GetWhetherValid(gnss_satellite_id)) {
      auto true_pos = true_info_.GetSatellitePositionEcef(gnss_satellite_id);
      auto true_clock = true_info_.GetSatelliteClock(gnss_satellite_id);
      auto estimation_pos = estimate_info_.GetSatellitePositionEcef(gnss_satellite_id);
      auto estimation_clock = estimate_info_.GetSatelliteClock(gnss_satellite_id);

      for (int i = 0; i < 3; ++i) {
        ofs_sa << fixed << setprecision(10) << estimation_pos[i] - true_pos[i] << ",";
      }
      ofs_sa << fixed << setprecision(10) << estimation_clock - true_clock << ",";
    } else {
      for (int i = 0; i < 4; ++i) {
        ofs_sa << 0.0 << ",";
      }
    }
  }

  ofs_true << endl;
  ofs_estimation << endl;
  ofs_sa << endl;
#endif
  return;
}
