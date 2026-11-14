# Networked AR profile: simulation summary

Transport-queue mode, conditional networking results. `L` is input to modeled correct effect in ms (publication plus the
10 ms input, 30 ms display and 1 ms uncertainty allowances). Counts are summed over seeds; maxima are the worst seed.

- analytic checks: 0 failures
- verdict: F0 INFEASIBLE as written (expected: the profile requires this to be flagged)


## qualification

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Q0 | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 53.8 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| Q1 | 10 | 2280 | 2280 | 0 | 0 | 61.3 / 61.3 / 64.9 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| Q2 | 10 | 2280 | 2280 | 0 | 0 | 72.4 / 72.4 / 76.1 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |

## R0 independent loss

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| loss 0 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 53.8 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 53.8 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 53.8 | 33.1 | 1.9 | 105240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.01 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 64.8 | 33.1 | 1.9 | 104240 | 89.4 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.01 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 64.8 | 33.1 | 1.9 | 104288 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.01 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 62.7 | 33.1 | 1.9 | 104496 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.03 both ways | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.6 / 68.9 | 33.1 | 1.9 | 104152 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.03 A to B | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 66.3 | 33.1 | 1.9 | 104328 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.03 B to A | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 50.2 / 64.9 | 33.1 | 1.9 | 103784 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |

## J0 jitter

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| jitter 0 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| jitter 0 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 60.5 / 60.5 / 62.7 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| jitter 0 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.6 / 71.6 / 73.8 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| jitter 5 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 50.1 / 53.9 / 56.6 | 30.9 | 6.9 | 106072 | 89.5 | 48.9 | 70.1 | 3/3 | 3/3 |
| jitter 5 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 61.3 / 65.2 / 67.6 | 30.9 | 6.9 | 106072 | 89.5 | 48.9 | 70.0 | 3/3 | 3/3 |
| jitter 5 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 72.4 / 76.2 / 78.8 | 30.9 | 6.9 | 106072 | 89.4 | 48.9 | 70.0 | 3/3 | 3/3 |
| jitter 10 ms, 0 erased | 3 | 684 | 684 | 0 | 0 | 51.8 / 58.2 / 61.4 | 30.9 | 11.9 | 106072 | 89.5 | 48.9 | 73.4 | 3/3 | 3/3 |
| jitter 10 ms, 1 erased | 3 | 684 | 684 | 0 | 0 | 63.0 / 69.6 / 72.6 | 30.9 | 11.9 | 106072 | 89.7 | 48.9 | 73.4 | 3/3 | 3/3 |
| jitter 10 ms, 2 erased | 3 | 684 | 684 | 0 | 0 | 74.2 / 80.7 / 83.7 | 30.9 | 11.9 | 103968 | 89.2 | 48.9 | 73.4 | 3/3 | 3/3 |

## C0 shared blockage

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 copy periods blocked | 3 | 684 | 684 | 0 | 0 | 60.5 / 60.5 / 62.7 | 30.9 | 1.9 | 104544 | 89.4 | 48.9 | 66.9 | 3/3 | 3/3 |
| 2 copy periods blocked | 3 | 684 | 684 | 0 | 0 | 71.6 / 71.6 / 73.8 | 30.9 | 1.9 | 104544 | 89.4 | 48.9 | 66.9 | 3/3 | 3/3 |
| 3 copy periods blocked | 3 | 684 | 0 | 684 | 0 | 308.6 / 333.4 / 346.0 | 30.9 | 0.0 | 112176 | 94.7 | 48.9 | 66.9 | 0/3 | 0/3 |
| 5 copy periods blocked | 3 | 684 | 0 | 684 | 0 | 332.0 / 421.1 / 546.0 | 30.9 | 0.0 | 113032 | 94.6 | 48.9 | 66.9 | 0/3 | 0/3 |

## S0 severe loss

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| loss 0.05 | 10 | 2280 | 2280 | 0 | 0 | 50.2 / 53.0 / 68.1 | 33.1 | 1.9 | 106240 | 89.3 | 48.9 | 66.2 | 10/10 | 10/10 |
| loss 0.10 | 10 | 2280 | 2279 | 1 | 0 | 50.2 / 59.8 / 224.5 | 33.1 | 1.9 | 108112 | 89.4 | 48.9 | 78.8 | 9/10 | 9/10 |

## S0 blackout

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 50 ms blackout every 10 s | 10 | 2280 | 2268 | 12 | 0 | 50.2 / 70.6 / 241.1 | 33.1 | 1.9 | 106304 | 89.5 | 48.9 | 66.2 | 9/10 | 9/10 |
| 100 ms blackout every 10 s | 10 | 2280 | 2256 | 24 | 0 | 50.2 / 111.7 / 374.4 | 33.1 | 1.9 | 108584 | 89.5 | 49.0 | 66.2 | 8/10 | 8/10 |
| 250 ms blackout every 10 s | 10 | 2280 | 2244 | 36 | 0 | 50.2 / 170.9 / 507.8 | 33.1 | 1.9 | 109248 | 89.5 | 48.9 | 66.2 | 7/10 | 7/10 |

## P0 duplicate and reorder

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| reorder delay 5 ms | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 56.6 | 30.9 | 6.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| reorder delay 20 ms | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 62.7 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |

## O0 simultaneous inputs

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| burst 1, lending as written | 3 | 684 | 684 | 0 | 0 | 44.4 / 44.4 / 44.4 | 23.3 | 2.3 | 104160 | 89.7 | 48.9 | 60.9 | 3/3 | 3/3 |
| burst 2, lending as written | 3 | 1368 | 1368 | 0 | 0 | 44.8 / 45.2 / 45.2 | 23.6 | 2.9 | 116432 | 101.3 | 48.9 | 60.9 | 3/3 | 3/3 |
| burst 4, lending as written | 3 | 2736 | 2736 | 0 | 0 | 45.6 / 46.9 / 46.9 | 24.1 | 4.0 | 141168 | 124.6 | 48.9 | 60.9 | 3/3 | 3/3 |
| burst 4, standing urgent reserve 64 kbit | 3 | 2736 | 2736 | 0 | 0 | 45.6 / 46.9 / 46.9 | 24.1 | 4.0 | 141168 | 124.6 | 48.9 | 60.9 | 3/3 | 3/3 |
| burst 4, standing urgent reserve 72 kbit | 3 | 2736 | 2736 | 0 | 0 | 45.6 / 46.9 / 46.9 | 24.1 | 4.0 | 141168 | 124.6 | 48.9 | 60.9 | 3/3 | 3/3 |

## R1 partition and rejoin

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1000 ms partition, replay, takeover | 3 | 684 | 678 | 6 | 0 | 49.4 / 49.4 / 1210.1 | 30.9 | 2.1 | 107744 | 89.4 | 48.9 | 66.9 | 0/3 | 0/3 |

## sweep input rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.5 inputs/s | 3 | 342 | 342 | 0 | 0 | 49.2 / 49.2 / 52.4 | 31.7 | 1.9 | 104320 | 83.7 | 48.9 | 66.9 | 3/3 | 3/3 |
| 1 inputs/s | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 3 inputs/s | 3 | 2052 | 2052 | 0 | 0 | 47.9 / 47.9 / 52.7 | 32.0 | 1.9 | 129256 | 112.7 | 48.9 | 66.9 | 3/3 | 3/3 |

## sweep routine state

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 Hz, TAK v1 lean state | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 85496 | 72.5 | 32.5 | 98.3 | 3/3 | 3/3 |
| 15 Hz, TAK v1 lean state | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 30 Hz, TAK v1 lean state | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 158680 | 139.0 | 98.0 | 40.1 | 3/3 | 3/3 |

## sweep opportunity rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 30 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 68.9 / 68.9 / 70.7 | 94.5 | 1.9 | 104264 | 89.1 | 48.9 | 85.0 | 3/3 | 3/3 |
| 30 Hz, 1 erased | 3 | 684 | 342 | 342 | 0 | 102.2 / 102.2 / 104.0 | 94.5 | 1.9 | 104264 | 89.1 | 48.9 | 85.0 | 1/3 | 1/3 |
| 30 Hz, 2 erased | 3 | 684 | 0 | 684 | 0 | 135.5 / 135.5 / 137.4 | 94.5 | 1.9 | 104264 | 89.1 | 48.9 | 85.0 | 0/3 | 0/3 |
| 45 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 60.5 / 60.5 / 64.1 | 65.7 | 1.9 | 103960 | 89.2 | 48.9 | 78.4 | 3/3 | 3/3 |
| 45 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 82.7 / 82.7 / 86.3 | 65.7 | 1.9 | 103960 | 89.2 | 48.9 | 78.4 | 3/3 | 3/3 |
| 45 Hz, 2 erased | 3 | 684 | 228 | 456 | 0 | 104.9 / 104.9 / 108.6 | 65.7 | 1.9 | 103960 | 89.2 | 48.9 | 78.4 | 0/3 | 0/3 |
| 60 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 52.1 / 52.2 / 57.1 | 47.6 | 1.9 | 104408 | 89.4 | 48.9 | 72.5 | 3/3 | 3/3 |
| 60 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 68.8 / 68.8 / 73.8 | 47.6 | 1.9 | 104408 | 89.4 | 48.9 | 72.5 | 3/3 | 3/3 |
| 60 Hz, 2 erased | 3 | 684 | 684 | 0 | 0 | 85.5 / 85.5 / 90.5 | 47.6 | 1.9 | 104408 | 89.4 | 48.9 | 72.5 | 3/3 | 3/3 |
| 90 Hz, 0 erased | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 90 Hz, 1 erased | 3 | 684 | 684 | 0 | 0 | 60.5 / 60.5 / 62.7 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 90 Hz, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.6 / 71.6 / 73.8 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |

## sweep PHY rate

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 71.6 / 71.6 / 73.8 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 2000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 73.2 / 73.2 / 75.4 | 30.9 | 3.5 | 108184 | 89.5 | 48.9 | 68.5 | 3/3 | 3/3 |
| 1000 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 75.2 / 75.2 / 77.4 | 30.9 | 6.3 | 108184 | 89.5 | 48.9 | 70.6 | 3/3 | 3/3 |
| 500 kbit/s, 2 erased | 3 | 684 | 684 | 0 | 0 | 80.3 / 82.2 / 88.4 | 30.9 | 22.1 | 108184 | 89.4 | 48.9 | 75.6 | 3/3 | 2/3 |
| 200 kbit/s, 2 erased | 3 | 684 | 0 | 684 | 0 | 129.9 / 144.3 / 176.2 | 30.9 | 109.7 | 104240 | 88.9 | 48.9 | 120.9 | 0/3 | 0/3 |

## participants

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 nodes, unicast, lending as written | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 2 nodes, unicast, standing urgent reserve 64 kbit | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 2 nodes, common delivery | 3 | 684 | 684 | 0 | 0 | 49.4 / 49.4 / 51.6 | 30.9 | 1.9 | 104544 | 89.5 | 48.9 | 66.9 | 3/3 | 3/3 |
| 3 nodes, unicast, lending as written | 3 | 2052 | 2052 | 0 | 0 | 48.8 / 52.0 / 74.2 | 53.1 | 2.5 | 200000 | 179.2 | 97.7 | 68.0 | 3/3 | 0/3 |
| 3 nodes, unicast, standing urgent reserve 64 kbit | 3 | 2052 | 2052 | 0 | 0 | 48.8 / 52.0 / 53.1 | 32.3 | 2.8 | 164272 | 157.1 | 76.0 | 129.2 | 3/3 | 3/3 |
| 3 nodes, common delivery | 3 | 2052 | 2052 | 0 | 0 | 48.4 / 51.4 / 52.7 | 32.0 | 1.9 | 116456 | 89.7 | 48.9 | 67.7 | 3/3 | 3/3 |
| 4 nodes, unicast, lending as written | 3 | 4104 | 2548 | 1556 | 0 | 84.8 / 170.3 / 272.4 | 273.8 | 2.5 | 200000 | 198.3 | 78.9 | 204.1 | 0/3 | 0/3 |
| 4 nodes, unicast, standing urgent reserve 64 kbit | 3 | 4104 | 4104 | 0 | 0 | 50.8 / 53.5 / 53.9 | 32.9 | 3.4 | 176512 | 169.4 | 52.4 | 342.9 | 3/3 | 3/3 |
| 4 nodes, common delivery | 3 | 4104 | 4104 | 0 | 0 | 50.7 / 52.7 / 53.1 | 32.4 | 1.9 | 108720 | 89.4 | 49.0 | 67.6 | 3/3 | 3/3 |
| 8 nodes, unicast, lending as written | 3 | 19152 | 4408 | 14744 | 0 | 270.7 / 746.0 / 941.3 | 920.7 | 9.1 | 200000 | 198.2 | 36.4 | 913.6 | 0/3 | 0/3 |
| 8 nodes, unicast, standing urgent reserve 120 kbit | 3 | 19152 | 19152 | 0 | 0 | 49.9 / 54.9 / 61.1 | 34.8 | 10.8 | 193016 | 159.7 | 12.3 | 2000.7 | 3/3 | 0/3 |
| 8 nodes, common delivery | 3 | 19152 | 19152 | 0 | 0 | 49.0 / 53.1 / 54.0 | 33.3 | 2.2 | 111816 | 89.9 | 49.0 | 68.3 | 3/3 | 3/3 |
| 16 nodes, unicast, lending as written | 3 | 82080 | 8433 | 73647 | 0 | 440.8 / 932.6 / 1064.6 | 1767.4 | 12.6 | 200000 | 197.8 | 3.0 | 1770.6 | 0/3 | 0/3 |
| 16 nodes, common delivery | 3 | 82080 | 82080 | 0 | 0 | 48.4 / 53.4 / 54.0 | 33.3 | 2.3 | 119120 | 89.8 | 49.1 | 68.7 | 3/3 | 3/3 |
| 32 nodes, common delivery | 3 | 339264 | 339264 | 0 | 0 | 49.0 / 53.5 / 54.1 | 33.3 | 3.0 | 115480 | 89.6 | 49.1 | 69.2 | 3/3 | 3/3 |
| 8 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 19152 | 18952 | 200 | 0 | 73.5 / 79.1 / 598.9 | 33.3 | 7.1 | 114072 | 90.4 | 49.1 | 71.1 | 0/3 | 0/3 |
| 16 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 82080 | 81284 | 796 | 0 | 73.0 / 79.2 / 704.7 | 33.3 | 7.1 | 117864 | 90.4 | 49.1 | 71.8 | 0/3 | 0/3 |
| 32 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | 3 | 339264 | 335787 | 3477 | 0 | 73.7 / 79.4 / 893.3 | 33.3 | 7.8 | 120216 | 91.3 | 49.1 | 71.9 | 0/3 | 0/3 |
| 16 nodes, common delivery, PHY 10000 kbit/s | 3 | 82080 | 82080 | 0 | 0 | 48.4 / 53.4 / 54.0 | 33.3 | 2.3 | 119120 | 89.8 | 49.1 | 68.7 | 3/3 | 3/3 |
| 16 nodes, common delivery, PHY 5000 kbit/s | 3 | 82080 | 82080 | 0 | 0 | 48.8 / 53.9 / 54.8 | 33.3 | 4.0 | 118072 | 89.8 | 49.1 | 69.2 | 3/3 | 3/3 |
| 16 nodes, common delivery, PHY 2000 kbit/s | 3 | 82080 | 82080 | 0 | 0 | 51.3 / 56.9 / 62.2 | 33.3 | 16.1 | 118488 | 89.8 | 49.1 | 72.3 | 3/3 | 3/3 |
| 16 nodes, common delivery, PHY 1000 kbit/s | 3 | 82080 | 0 | 81195 | 885 | 2256.6 / 2323.6 / 2439.8 | 394.0 | 2193.8 | 82336 | 62.5 | 25.7 | 2420.5 | 0/3 | 0/3 |
| 16 nodes, common delivery, correlated 3% loss | 10 | 273600 | 273600 | 0 | 0 | 49.0 / 53.6 / 76.0 | 33.3 | 2.7 | 122312 | 89.8 | 49.1 | 69.2 | 10/10 | 10/10 |

## routes

| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 4 nodes, unicast, 1 hop, 1 input/s, reserve 56 kbit | 3 | 4104 | 4104 | 0 | 0 | 50.8 / 53.5 / 53.9 | 32.9 | 3.4 | 184552 | 177.3 | 58.5 | 283.2 | 3/3 | 3/3 |
| 4 nodes, unicast, 1 hop, 0.5 input/s, reserve 40 kbit | 3 | 2052 | 2052 | 0 | 0 | 48.8 / 52.7 / 54.1 | 33.1 | 3.4 | 196504 | 175.8 | 73.5 | 209.2 | 3/3 | 3/3 |
| 4 nodes, unicast, 2 hop, 1 input/s, reserve 104 kbit | 3 | 4104 | 4104 | 0 | 0 | 52.8 / 55.5 / 56.5 | 32.9 | 6.5 | 188664 | 164.4 | 8.1 | 2154.5 | 3/3 | 3/3 |
| 4 nodes, unicast, 2 hop, 0.5 input/s, reserve 80 kbit | 3 | 2052 | 2052 | 0 | 0 | 50.7 / 54.7 / 56.6 | 70.3 | 7.5 | 200000 | 153.8 | 14.9 | 1769.1 | 3/3 | 2/3 |
| 4 nodes, unicast, 3 hop, 1 input/s, reserve 152 kbit | 3 | 4104 | 4104 | 0 | 0 | 54.9 / 57.3 / 57.7 | 32.9 | 13.6 | 163904 | 151.1 | 0.2 | -1.0 | 3/3 | 3/3 |
| 4 nodes, unicast, 3 hop, 0.5 input/s, reserve 112 kbit | 3 | 2052 | 2052 | 0 | 0 | 52.6 / 56.6 / 61.5 | 33.1 | 14.5 | 197232 | 139.3 | 2.4 | 3878.6 | 3/3 | 3/3 |

## phase sweeps (12 input phases x 12 display phases x 3 boundary offsets, modeled photon)

- **Q0 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 53.892 ms, worst publication 13.011 ms, worst W 33.332 ms
- **Q1 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 65.003 ms, worst publication 24.122 ms, worst W 33.332 ms
- **Q2 phase sweep**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 76.114 ms, worst publication 35.233 ms, worst W 33.332 ms
- **Q2 phase sweep, D at its 20 ms limit**: 432 phase points, service pass 432, profile pass 432, 12096 of 12096 on time, worst L 93.707 ms, worst publication 53.093 ms, worst W 33.332 ms
