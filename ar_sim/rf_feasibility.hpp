#ifndef RF_FEASIBILITY_HPP
#define RF_FEASIBILITY_HPP

// Case F0 (NET-016): the inherited video and serial-ranging schedule. Places the
// study's ranging exchanges into the quiet time the stereo video bursts leave and
// reports every exchange that finds no slot. aGuardUs is taken from each end of
// every quiet window (guard and reacquisition); the profile requires it to be an
// explicit input. Returns the number of exchanges left unplaced per superframe.
int RunRfFeasibility(long long aGuardUs);

#endif
