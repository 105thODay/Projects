#ifndef SIM_TIME_HPP
#define SIM_TIME_HPP

#include <cstdint>
#include <stdexcept>

// Simulation truth time. One tick is one ninth of a microsecond, so that every
// period the profile names (1/90 s, 1/60 s, 1/45 s, 1/30 s, 1/15 s, 1/7.5 s) and
// every microsecond offset is an exact integer. The profile forbids rounding a
// deadline down; with this unit nothing is rounded at all.
typedef int64_t SimTime;

struct TimeUnit
{
   static constexpr int64_t cPER_US = 9;             // ticks per microsecond
   static constexpr int64_t cPER_MS = 9000;          // ticks per millisecond
   static constexpr int64_t cPER_S  = 9000000;       // ticks per second
};

inline SimTime FromUs(int64_t aMicroseconds)
{
   return aMicroseconds * TimeUnit::cPER_US;
}

inline double ToMs(SimTime aTime)
{
   return static_cast<double>(aTime) / static_cast<double>(TimeUnit::cPER_MS);
}

inline double ToUs(SimTime aTime)
{
   return static_cast<double>(aTime) / static_cast<double>(TimeUnit::cPER_US);
}

// Exact period of a rate given in millihertz. Throws when the period is not a
// whole number of ticks, so an inexact rate can never slip into a run.
inline SimTime PeriodFromMilliHz(int64_t aMilliHz)
{
   const int64_t numerator = TimeUnit::cPER_S * 1000;
   if (aMilliHz <= 0 || numerator % aMilliHz != 0)
   {
      throw std::invalid_argument("rate has no exact period in ninths of a microsecond");
   }
   return numerator / aMilliHz;
}

#endif
