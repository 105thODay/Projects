# Measured CoT sizes against human-limited input (case Q2, 3 seeds, 60 s)

Cell: share of inputs late, then state age P95. 'cap' marks a reserve cut to fit the ceiling.

## unicast

| encoding (urgent B charged) | players | inputs/s the ceiling can promise | renewal | string4 | string7 | string10 |
|---|---|---|---|---|---|---|
| CoT XML, as specified (1275) | 8 | 0 | 99.01 % late; no state; cap | 99.09 % late; no state; cap | 99.29 % late; no state; cap | 99.48 % late; no state; cap |
| CoT XML, as specified (1275) | 12 | 0 | 99.99 % late; no state; cap | 99.99 % late; no state; cap | 99.99 % late; no state; cap | 99.99 % late; no state; cap |
| CoT XML, as specified (1275) | 16 | 0 | 100.0 % late; no state; cap | 100.0 % late; no state; cap | 99.99 % late; no state; cap | 100.0 % late; no state; cap |
| CoT XML, lean schema (712) | 8 | 1 | 68.45 % late; no state; cap | 86.46 % late; no state; cap | 92.09 % late; no state; cap | 94.66 % late; no state; cap |
| CoT XML, lean schema (712) | 12 | 0 | 95.42 % late; no state; cap | 96.38 % late; no state; cap | 97.14 % late; no state; cap | 97.92 % late; no state; cap |
| CoT XML, lean schema (712) | 16 | 0 | 99.9 % late; no state; cap | 99.88 % late; no state; cap | 99.93 % late; no state; cap | 99.91 % late; no state; cap |
| TAK v1, lean schema (556) | 8 | 1 | 26.59 % late; no state; cap | 68.47 % late; no state; cap | 82.09 % late; no state; cap | 88.13 % late; no state; cap |
| TAK v1, lean schema (556) | 12 | 1 | 79.57 % late; no state; cap | 89.22 % late; no state; cap | 92.7 % late; no state; cap | 95.25 % late; no state; cap |
| TAK v1, lean schema (556) | 16 | 0 | 97.2 % late; no state; cap | 97.37 % late; no state; cap | 97.94 % late; no state; cap | 98.13 % late; no state; cap |
| XML + session dictionary, as specified (234) | 8 | 4 | 1.72 % late; 270 ms | 9.36 % late; 751 ms | 35.65 % late; no state; cap | 57.46 % late; no state; cap |
| XML + session dictionary, as specified (234) | 12 | 2 | 2.46 % late; no state; cap | 38.24 % late; no state; cap | 64.34 % late; no state; cap | 76.17 % late; no state; cap |
| XML + session dictionary, as specified (234) | 16 | 2 | 21.58 % late; no state; cap | 66.9 % late; no state; cap | 78.85 % late; no state; cap | 85.71 % late; no state; cap |
| XML + session dictionary, lean (191) | 8 | 5 | 1.41 % late; 166 ms | 7.83 % late; 367 ms | 14.97 % late; no state; cap | 43.42 % late; no state; cap |
| XML + session dictionary, lean (191) | 12 | 3 | 2.36 % late; 815 ms | 25.63 % late; no state; cap | 57.07 % late; no state; cap | 71.59 % late; no state; cap |
| XML + session dictionary, lean (191) | 16 | 2 | 14.83 % late; no state; cap | 57.47 % late; no state; cap | 74.96 % late; no state; cap | 82.59 % late; no state; cap |
| binary AR detail, lean (new TAK version) (301) | 8 | 3 | 1.92 % late; 849 ms | 30.48 % late; no state; cap | 60.06 % late; no state; cap | 72.84 % late; no state; cap |
| binary AR detail, lean (new TAK version) (301) | 12 | 2 | 20.73 % late; no state; cap | 65.15 % late; no state; cap | 78.08 % late; no state; cap | 85.58 % late; no state; cap |
| binary AR detail, lean (new TAK version) (301) | 16 | 1 | 64.03 % late; no state; cap | 85.48 % late; no state; cap | 90.95 % late; no state; cap | 93.68 % late; no state; cap |

## common

| encoding (urgent B charged) | players | inputs/s the ceiling can promise | renewal | string4 | string7 | string10 |
|---|---|---|---|---|---|---|
| CoT XML, as specified (1275) | 8 | 6 | 0.77 % late; 99 ms | 8.52 % late; 208 ms | 15.0 % late; no state; cap | 43.25 % late; no state; cap |
| CoT XML, as specified (1275) | 12 | 6 | 0.92 % late; 101 ms | 8.21 % late; 206 ms | 14.92 % late; no state; cap | 43.47 % late; no state; cap |
| CoT XML, as specified (1275) | 16 | 6 | 0.91 % late; 100 ms | 9.48 % late; 205 ms | 14.98 % late; no state; cap | 43.32 % late; no state; cap |
| CoT XML, lean schema (712) | 8 | 11 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 718 ms |
| CoT XML, lean schema (712) | 12 | 11 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 724 ms |
| CoT XML, lean schema (712) | 16 | 11 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 727 ms |
| TAK v1, lean schema (556) | 8 | 14 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 98 ms |
| TAK v1, lean schema (556) | 12 | 14 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 100 ms |
| TAK v1, lean schema (556) | 16 | 14 | on time; 99 ms | on time; 99 ms | on time; 99 ms | on time; 99 ms |
| XML + session dictionary, as specified (234) | 8 | 34 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 98 ms |
| XML + session dictionary, as specified (234) | 12 | 34 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 100 ms |
| XML + session dictionary, as specified (234) | 16 | 34 | on time; 99 ms | on time; 99 ms | on time; 99 ms | on time; 99 ms |
| XML + session dictionary, lean (191) | 8 | 41 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 98 ms |
| XML + session dictionary, lean (191) | 12 | 41 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 100 ms |
| XML + session dictionary, lean (191) | 16 | 41 | on time; 99 ms | on time; 99 ms | on time; 99 ms | on time; 99 ms |
| binary AR detail, lean (new TAK version) (301) | 8 | 26 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 98 ms |
| binary AR detail, lean (new TAK version) (301) | 12 | 26 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 100 ms |
| binary AR detail, lean (new TAK version) (301) | 16 | 26 | on time; 99 ms | on time; 99 ms | on time; 99 ms | on time; 99 ms |

