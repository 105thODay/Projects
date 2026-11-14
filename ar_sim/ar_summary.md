# Networked AR profile: simulation summary

Transport-queue mode, conditional networking results. `L` is input to modeled correct effect in ms (publication plus the
10 ms input, 30 ms display and 1 ms uncertainty allowances). Counts are summed over seeds; maxima are the worst seed.

- analytic checks: 0 failures
- verdict: F0 INFEASIBLE as written (expected: the profile requires this to be flagged)


## qualification

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Q0 | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 54.1 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| Q1 | 10 | 2280 | 2280 | 0 | 0 | 61.5 / 61.5 / 65.2 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| Q2 | 10 | 2280 | 2280 | 0 | 0 | 72.6 / 72.6 / 76.3 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |

## R0 independent loss

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| loss 0 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 54.1 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 54.1 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 54.1 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.01 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 65.2 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.01 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 65.0 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.01 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 65.2 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.03 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.9 / 69.0 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.03 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 74.7 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.03 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 50.4 / 65.2 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |

## J0 jitter

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| jitter 0 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| jitter 0 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 60.7 / 60.7 / 62.9 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| jitter 0 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.8 / 71.8 / 74.0 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| jitter 5 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 50.5 / 54.1 / 56.8 | 30.9 | 7.1 | 152000 | 146.2 | 96.0 | 70.4 | 3/3 | 3/3 |
| jitter 5 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 61.6 / 65.1 / 67.9 | 30.9 | 7.1 | 152000 | 146.2 | 96.0 | 70.4 | 3/3 | 3/3 |
| jitter 5 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 72.7 / 76.4 / 79.0 | 30.9 | 7.1 | 152000 | 146.2 | 96.0 | 70.4 | 3/3 | 3/3 |
| jitter 10 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 52.2 / 58.7 / 61.7 | 30.9 | 12.1 | 152000 | 146.2 | 96.0 | 73.7 | 3/3 | 3/3 |
| jitter 10 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 63.3 / 69.6 / 72.8 | 30.9 | 12.1 | 152000 | 146.2 | 96.0 | 73.6 | 3/3 | 3/3 |
| jitter 10 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 74.5 / 81.0 / 84.0 | 30.9 | 12.1 | 152000 | 146.2 | 96.0 | 73.6 | 3/3 | 3/3 |

## C0 shared blockage

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 copy periods blocked | 3 | 684 | 684 | 0 | 0 | 60.7 / 60.7 / 62.9 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 2 copy periods blocked | 3 | 684 | 684 | 0 | 0 | 71.8 / 71.8 / 74.0 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 3 copy periods blocked | 3 | 684 | 0 | 684 | 0 | 308.9 / 333.7 / 346.3 | 30.9 | 0.0 | 168000 | 155.4 | 96.0 | 67.2 | 0/3 | 0/3 |
| 5 copy periods blocked | 3 | 684 | 0 | 684 | 0 | 332.3 / 421.4 / 546.3 | 30.9 | 0.0 | 168000 | 155.6 | 96.0 | 67.2 | 0/3 | 0/3 |

## S0 severe loss

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| loss 0.05 | 10 | 2280 | 2280 | 0 | 0 | 50.4 / 54.3 / 76.3 | 33.1 | 2.1 | 152000 | 146.2 | 96.0 | 66.5 | 10/10 | 10/10 |
| loss 0.10 | 10 | 2280 | 2278 | 2 | 0 | 50.4 / 58.7 / 291.4 | 33.1 | 2.1 | 155200 | 146.2 | 96.0 | 86.7 | 9/10 | 9/10 |

## S0 blackout

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 50 ms blackout every 10 s | 10 | 2280 | 2268 | 12 | 0 | 50.4 / 70.8 / 241.3 | 33.1 | 2.1 | 155200 | 146.2 | 96.0 | 66.5 | 9/10 | 9/10 |
| 100 ms blackout every 10 s | 10 | 2280 | 2256 | 24 | 0 | 50.4 / 112.0 / 374.7 | 33.1 | 2.1 | 155200 | 146.3 | 96.0 | 66.5 | 8/10 | 8/10 |
| 250 ms blackout every 10 s | 10 | 2280 | 2244 | 36 | 0 | 50.4 / 171.2 / 508.0 | 33.1 | 2.1 | 155200 | 146.3 | 96.0 | 66.5 | 7/10 | 7/10 |

## P0 duplicate and reorder

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| reorder delay 5 ms | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 56.8 | 30.9 | 7.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| reorder delay 20 ms | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 62.9 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |

## O0 simultaneous inputs

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| burst 1, lending as written | 3 | 684 | 684 | 0 | 0 | 44.9 / 44.9 / 44.9 | 23.3 | 2.8 | 152000 | 146.2 | 96.0 | 61.7 | 3/3 | 3/3 |
| burst 2, lending as written | 3 | 1368 | 1368 | 0 | 0 | 45.5 / 46.2 / 46.2 | 23.6 | 3.8 | 171200 | 164.5 | 96.0 | 61.7 | 3/3 | 3/3 |
| burst 4, lending as written | 3 | 2736 | 1338 | 1398 | 0 | 100.4 / 189.3 / 222.0 | 201.1 | 5.9 | 200000 | 192.6 | 87.8 | 79.9 | 0/3 | 0/3 |
| burst 4, standing urgent reserve 64 kbit | 3 | 2736 | 1338 | 1398 | 0 | 100.4 / 189.3 / 222.0 | 201.1 | 5.9 | 200000 | 192.6 | 87.8 | 79.9 | 0/3 | 0/3 |
| burst 4, standing urgent reserve 104 kbit | 3 | 2736 | 2736 | 0 | 0 | 46.8 / 48.7 / 48.7 | 24.1 | 5.9 | 172800 | 165.1 | 60.2 | 129.9 | 3/3 | 3/3 |

## R1 partition and rejoin

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1000 ms partition, replay, takeover | 3 | 684 | 678 | 6 | 0 | 49.6 / 49.6 / 1210.7 | 30.9 | 2.5 | 168000 | 146.5 | 96.0 | 67.2 | 0/3 | 0/3 |

## sweep input rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.5 inputs/s | 3 | 342 | 342 | 0 | 0 | 49.5 / 49.5 / 52.6 | 31.7 | 2.1 | 152000 | 137.1 | 96.0 | 67.2 | 3/3 | 3/3 |
| 1 inputs/s | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 3 inputs/s | 3 | 2052 | 2052 | 0 | 0 | 48.1 / 48.1 / 52.9 | 32.0 | 2.1 | 190400 | 182.7 | 96.0 | 67.2 | 3/3 | 3/3 |

## sweep routine state

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 Hz, 400 B | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 84800 | 82.2 | 32.0 | 98.2 | 3/3 | 3/3 |
| 10 Hz, 800 B | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 120000 | 114.2 | 64.0 | 98.6 | 3/3 | 3/3 |
| 15 Hz, 400 B | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 100800 | 98.2 | 48.0 | 66.9 | 3/3 | 3/3 |
| 15 Hz, 800 B | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 30 Hz, 400 B | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 148800 | 146.2 | 96.0 | 40.0 | 3/3 | 3/3 |
| 30 Hz, 800 B | 3 | 684 | 684 | 0 | 0 | 69.4 / 69.4 / 87.8 | 96.4 | 2.1 | 200000 | 198.4 | 148.2 | 63.4 | 3/3 | 0/3 |

## sweep opportunity rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 30 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 69.1 / 69.1 / 70.9 | 94.5 | 2.1 | 152000 | 146.2 | 96.0 | 85.3 | 3/3 | 3/3 |
| 30 Hz, 1 erased | 3 | 684 | 342 | 342 | 0 | 102.4 / 102.4 / 104.3 | 94.5 | 2.1 | 152000 | 146.2 | 96.0 | 85.3 | 1/3 | 1/3 |
| 30 Hz, 2 erased | 3 | 684 | 0 | 684 | 0 | 135.8 / 135.8 / 137.6 | 94.5 | 2.1 | 152000 | 146.2 | 96.0 | 85.3 | 0/3 | 0/3 |
| 45 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 60.7 / 60.7 / 64.3 | 65.7 | 2.1 | 152000 | 146.2 | 96.0 | 78.7 | 3/3 | 3/3 |
| 45 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 82.9 / 82.9 / 86.6 | 65.7 | 2.1 | 152000 | 146.2 | 96.0 | 78.7 | 3/3 | 3/3 |
| 45 Hz, 2 erased | 3 | 684 | 228 | 456 | 0 | 105.2 / 105.2 / 108.8 | 65.7 | 2.1 | 152000 | 146.2 | 96.0 | 78.7 | 0/3 | 0/3 |
| 60 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 52.4 / 52.4 / 57.4 | 47.6 | 2.1 | 152000 | 146.2 | 96.0 | 72.8 | 3/3 | 3/3 |
| 60 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 69.1 / 69.1 / 74.0 | 47.6 | 2.1 | 152000 | 146.2 | 96.0 | 72.8 | 3/3 | 3/3 |
| 60 Hz, 2 erased | 3 | 684 | 684 | 0 | 0 | 85.7 / 85.7 / 90.7 | 47.6 | 2.1 | 152000 | 146.2 | 96.0 | 72.8 | 3/3 | 3/3 |
| 90 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 90 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 60.7 / 60.7 / 62.9 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 90 Hz, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.8 / 71.8 / 74.0 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |

## sweep PHY rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.8 / 71.8 / 74.0 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 2000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 74.4 / 74.4 / 76.6 | 30.9 | 4.7 | 152000 | 146.2 | 96.0 | 70.2 | 3/3 | 3/3 |
| 1000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 78.1 / 79.2 / 79.8 | 30.9 | 12.8 | 155200 | 146.2 | 96.0 | 74.6 | 3/3 | 3/3 |
| 500 kbit/s, 2 erased | 3 | 684 | 456 | 228 | 0 | 99.3 / 103.5 / 105.0 | 30.9 | 36.9 | 155200 | 146.2 | 96.0 | 84.5 | 0/3 | 0/3 |
| 200 kbit/s, 2 erased | 3 | 684 | 0 | 684 | 0 | 1189.1 / 1240.2 / 1308.7 | 267.8 | 1033.5 | 120000 | 100.0 | 51.8 | 1249.8 | 0/3 | 0/3 |

## participants

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 nodes, unicast, lending as written | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 2 nodes, unicast, standing urgent reserve 64 kbit | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 2 nodes, common delivery | 3 | 684 | 684 | 0 | 0 | 49.6 / 49.6 / 51.8 | 30.9 | 2.1 | 152000 | 146.2 | 96.0 | 67.2 | 3/3 | 3/3 |
| 3 nodes, unicast, lending as written | 3 | 2052 | 1513 | 539 | 0 | 70.9 / 181.2 / 252.9 | 243.1 | 2.8 | 200000 | 196.4 | 98.4 | 205.9 | 0/3 | 0/3 |
| 3 nodes, unicast, standing urgent reserve 64 kbit | 3 | 2052 | 2052 | 0 | 0 | 49.3 / 52.6 / 53.6 | 32.3 | 4.1 | 180800 | 170.9 | 77.1 | 283.9 | 3/3 | 3/3 |
| 3 nodes, common delivery | 3 | 2052 | 2052 | 0 | 0 | 48.6 / 51.7 / 52.9 | 32.0 | 2.4 | 155200 | 146.2 | 96.0 | 67.9 | 3/3 | 3/3 |
| 4 nodes, unicast, lending as written | 3 | 4104 | 2098 | 2006 | 0 | 98.0 / 473.3 / 626.6 | 627.9 | 2.9 | 200000 | 198.9 | 70.2 | 581.5 | 0/3 | 0/3 |
| 4 nodes, unicast, standing urgent reserve 80 kbit | 3 | 4104 | 4104 | 0 | 0 | 51.2 / 54.3 / 54.6 | 32.9 | 4.5 | 184000 | 174.4 | 44.5 | 771.2 | 3/3 | 3/3 |
| 4 nodes, common delivery | 3 | 4104 | 4104 | 0 | 0 | 51.0 / 53.0 / 53.3 | 32.4 | 2.4 | 155200 | 146.2 | 96.0 | 67.9 | 3/3 | 3/3 |
| 8 nodes, unicast, lending as written | 3 | 19152 | 2538 | 16614 | 0 | 350.7 / 852.4 / 1024.7 | 1003.2 | 11.9 | 200000 | 199.0 | 24.6 | 2390.9 | 0/3 | 0/3 |
| 8 nodes, unicast, standing urgent reserve 184 kbit | 3 | 19152 | 19152 | 0 | 0 | 51.2 / 56.6 / 58.9 | 34.8 | 8.6 | 176000 | 143.7 | 0.1 | -1.0 | 3/3 | 0/3 |
| 8 nodes, common delivery | 3 | 19152 | 19152 | 0 | 0 | 49.3 / 53.4 / 54.2 | 33.3 | 2.6 | 155200 | 146.2 | 96.0 | 68.6 | 3/3 | 3/3 |
| 16 nodes, unicast, lending as written | 3 | 82080 | 27 | 56321 | 25732 | 18613.3 / 34869.2 / 37398.7 | 37636.6 | 11.2 | 200000 | 197.2 | 4.5 | 1279.5 | 0/3 | 0/3 |
| 16 nodes, common delivery | 3 | 82080 | 82080 | 0 | 0 | 48.7 / 53.7 / 54.2 | 33.3 | 2.7 | 155200 | 146.2 | 96.0 | 69.0 | 3/3 | 3/3 |
| 32 nodes, common delivery | 3 | 339264 | 339264 | 0 | 0 | 49.4 / 54.0 / 54.5 | 33.3 | 3.6 | 155200 | 146.2 | 96.0 | 69.8 | 3/3 | 3/3 |
| 8 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 19152 | 18949 | 203 | 0 | 73.9 / 79.5 / 624.4 | 33.3 | 7.7 | 164800 | 146.9 | 96.0 | 71.4 | 0/3 | 0/3 |
| 16 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 82080 | 81282 | 798 | 0 | 73.3 / 79.5 / 800.3 | 33.3 | 8.1 | 174400 | 147.6 | 96.0 | 72.1 | 0/3 | 0/3 |
| 32 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 339264 | 335848 | 3416 | 0 | 74.1 / 79.8 / 1009.5 | 33.3 | 8.9 | 184000 | 149.1 | 96.0 | 72.3 | 0/3 | 0/3 |
| 16 nodes, common delivery, PHY 10000 kbit/s | 3 | 82080 | 82080 | 0 | 0 | 48.7 / 53.7 / 54.2 | 33.3 | 2.7 | 155200 | 146.2 | 96.0 | 69.0 | 3/3 | 3/3 |
| 16 nodes, common delivery, PHY 5000 kbit/s | 3 | 82080 | 82080 | 0 | 0 | 49.4 / 54.7 / 55.7 | 33.3 | 5.2 | 155200 | 146.2 | 96.0 | 70.0 | 3/3 | 3/3 |
| 16 nodes, common delivery, PHY 2000 kbit/s | 3 | 82080 | 0 | 82080 | 0 | 643.9 / 685.1 / 746.5 | 242.7 | 592.3 | 148800 | 125.0 | 75.1 | 715.4 | 0/3 | 0/3 |
| 16 nodes, common delivery, PHY 1000 kbit/s | 3 | 82080 | 0 | 81135 | 945 | 2291.1 / 2477.3 / 2767.9 | 806.9 | 2200.3 | 99200 | 62.5 | 21.7 | 2738.5 | 0/3 | 0/3 |
| 16 nodes, common delivery, correlated 3% loss | 10 | 273600 | 273600 | 0 | 0 | 49.3 / 54.0 / 75.7 | 33.3 | 3.5 | 155200 | 146.2 | 96.0 | 69.6 | 10/10 | 10/10 |

## routes

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 4 nodes, unicast, 1 hop, 1 input/s, reserve 80 kbit | 3 | 4104 | 4104 | 0 | 0 | 51.2 / 54.3 / 54.6 | 32.9 | 4.5 | 184000 | 174.4 | 44.5 | 771.2 | 3/3 | 3/3 |
| 4 nodes, unicast, 1 hop, 0.5 input/s, reserve 64 kbit | 3 | 2052 | 2052 | 0 | 0 | 49.2 / 53.5 / 54.8 | 33.1 | 5.1 | 193600 | 163.0 | 57.1 | 663.8 | 3/3 | 3/3 |
| 4 nodes, unicast, 2 hop, 1 input/s, reserve 160 kbit | 3 | 4104 | 4104 | 0 | 0 | 53.9 / 56.5 / 56.9 | 32.9 | 8.3 | 180800 | 149.4 | 0.7 | 3488.7 | 3/3 | 3/3 |
| 4 nodes, unicast, 2 hop, 0.5 input/s, reserve 120 kbit | 3 | 2052 | 2052 | 0 | 0 | 51.5 / 55.6 / 58.7 | 81.4 | 8.6 | 200000 | 134.7 | 5.1 | 2834.4 | 3/3 | 2/3 |
| 4 nodes, unicast, 3 hop, 1 input/s, reserve 232 kbit (over the ceiling: run with 192) | 3 | 4104 | 4104 | 0 | 0 | 56.2 / 59.2 / 60.8 | 32.9 | 12.2 | 187200 | 172.2 | 0.0 | -1.0 | 3/3 | 3/3 |
| 4 nodes, unicast, 3 hop, 0.5 input/s, reserve 176 kbit | 3 | 2052 | 2052 | 0 | 0 | 53.9 / 57.7 / 59.1 | 33.1 | 12.2 | 196800 | 106.1 | 0.1 | -1.0 | 3/3 | 3/3 |

## phase sweeps (12 input phases x 12 display phases x 3 boundary offsets, modeled photon)

- **Q0 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 53.892 ms, worst publication 13.250 ms, worst W 33.332 ms
- **Q1 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 65.003 ms, worst publication 24.361 ms, worst W 33.332 ms
- **Q2 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 76.114 ms, worst publication 35.472 ms, worst W 33.332 ms
- **Q2 phase sweep, D at its 20 ms limit**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 93.707 ms, worst publication 53.332 ms, worst W 33.332 ms
