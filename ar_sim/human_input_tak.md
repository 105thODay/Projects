# TAK Protocol v1 on the wire against human-limited input (case Q2, 3 seeds, 60 s)

Cell: share of inputs late, then state age P95. 'cap' marks a reserve cut to fit the ceiling.

## unicast

| encoding (urgent B charged) | players | inputs/s the ceiling can promise | renewal | string4 | string7 | string10 |
|---|---|---|---|---|---|---|
| TAK v1, lean schema (513) | 8 | 2 | 20.66 % late; no state; cap | 64.83 % late; no state; cap | 78.56 % late; no state; cap | 86.02 % late; no state; cap |
| TAK v1, lean schema (513) | 12 | 1 | 74.93 % late; no state; cap | 88.67 % late; no state; cap | 91.91 % late; no state; cap | 94.96 % late; no state; cap |
| TAK v1, lean schema (513) | 16 | 0 | 92.48 % late; no state; cap | 95.42 % late; no state; cap | 96.48 % late; no state; cap | 97.16 % late; no state; cap |
| TAK v1, as specified (1071) | 8 | 0 | 91.86 % late; no state; cap | 94.53 % late; no state; cap | 96.57 % late; no state; cap | 97.37 % late; no state; cap |
| TAK v1, as specified (1071) | 12 | 0 | 99.94 % late; no state; cap | 99.93 % late; no state; cap | 99.95 % late; no state; cap | 99.94 % late; no state; cap |
| TAK v1, as specified (1071) | 16 | 0 | 100.0 % late; no state; cap | 100.0 % late; no state; cap | 100.0 % late; no state; cap | 100.0 % late; no state; cap |

## common

| encoding (urgent B charged) | players | inputs/s the ceiling can promise | renewal | string4 | string7 | string10 |
|---|---|---|---|---|---|---|
| TAK v1, lean schema (513) | 8 | 15 | on time; 98 ms | on time; 98 ms | on time; 98 ms | on time; 98 ms |
| TAK v1, lean schema (513) | 12 | 15 | on time; 100 ms | on time; 100 ms | on time; 100 ms | on time; 100 ms |
| TAK v1, lean schema (513) | 16 | 15 | on time; 99 ms | on time; 99 ms | on time; 99 ms | on time; 99 ms |
| TAK v1, as specified (1071) | 8 | 7 | on time; 98 ms | 4.5 % late; 98 ms | on time; 31330 ms | 30.93 % late; no state; cap |
| TAK v1, as specified (1071) | 12 | 7 | on time; 100 ms | 4.51 % late; 100 ms | on time; 22069 ms | 31.7 % late; no state; cap |
| TAK v1, as specified (1071) | 16 | 7 | on time; 100 ms | 4.53 % late; 100 ms | on time; 23810 ms | 31.43 % late; no state; cap |

