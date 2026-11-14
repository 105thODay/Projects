
### unicast 200 B: 8 nodes, 3 copies, state 10 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 55 | 126.1 | 3/3 |
| 2 erased | 0 | 77 | 126.1 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 82 | 128.2 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 87 | 130.7 | 3/3 |
| independent loss 0.01 | 0 | 75 | 126.8 | 3/3 |
| independent loss 0.03 | 2 of 19180 | 2090 | 129.0 | 2/3 |
| independent loss 0.05 | 5 of 19180 | 2090 | 130.6 | 0/3 |
| independent loss 0.10 | 18 of 19180 | 2643 | 143.7 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 1.0 % | 2864 | 130.6 | 0/3 |
| 1 copy periods blocked | 0 | 77 | 126.8 | 3/3 |
| 2 copy periods blocked | 1.7 % | 4106 | 129.9 | 0/3 |
| 3 copy periods blocked | 100.0 % | 27348 | 182.1 | 0/3 |
| 5 copy periods blocked | 100.0 % | 23292 | 187.7 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 1.7 % | 4049 | 164.3 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 100.0 % | 17196 | 236.7 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 40210 | 570.2 | 0/3 |
| 50 ms blackout every 10 s | 0.4 % | 3008 | 126.9 | 0/3 |
| 100 ms blackout every 10 s | 0.9 % | 3008 | 127.9 | 0/3 |
| 250 ms blackout every 10 s | 2.4 % | 3274 | 130.0 | 0/3 |
| duplicates and reordering | 0 | 72 | 126.3 | 3/3 |
| 2 simultaneous inputs, reserve 89.6 kbit | 0 | 62 | 211.0 | 3/3 |
| 4 simultaneous inputs, reserve 179.2 kbit | 0 | 80 | 6233.6 | 3/3 |
| 1000 ms partition, replay, takeover | 0.2 % | 3705 | 126.3 | 0/3 |
| 2 hop routes, reserve 134.4 kbit | 0 | 60 | 1147.1 | 3/3 |
| 2 erased, PHY 5000 kbit/s | 0 | 81 | 127.2 | 3/3 |
| 2 erased, PHY 2000 kbit/s | 1.2 % | 117 | 139.2 | 0/3 |
| 2 erased, PHY 1000 kbit/s | 100.0 % | 567 | 588.3 | 0/3 |
| 2 erased, opportunities at 60 Hz | 0 | 95 | 139.0 | 3/3 |
| 2 erased, opportunities at 45 Hz | 44.4 % | 111 | 143.0 | 0/3 |

### unicast 200 B: 12 nodes, 3 copies, state 5 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 61 | 414.3 | 3/3 |
| 2 erased | 0 | 82 | 414.3 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 86 | 416.7 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 90 | 419.1 | 3/3 |
| independent loss 0.01 | 0 | 69 | 418.4 | 3/3 |
| independent loss 0.03 | 3 of 45122 | 1242 | 427.0 | 1/3 |
| independent loss 0.05 | 7 of 45122 | 2382 | 436.4 | 0/3 |
| independent loss 0.10 | 0.1 % | 5950 | 462.4 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 1.1 % | 2964 | 422.1 | 0/3 |
| 1 copy periods blocked | 0 | 79 | 418.8 | 3/3 |
| 2 copy periods blocked | 5.4 % | 7221 | 431.4 | 0/3 |
| 3 copy periods blocked | 100.0 % | 64191 | 650.7 | 0/3 |
| 5 copy periods blocked | 100.0 % | 67032 | 656.6 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 5.2 % | 5044 | 502.8 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 100.0 % | 89079 | 975.1 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 83099 | 1897.6 | 0/3 |
| 50 ms blackout every 10 s | 0.3 % | 3976 | 416.3 | 0/3 |
| 100 ms blackout every 10 s | 1.0 % | 3980 | 419.2 | 0/3 |
| 250 ms blackout every 10 s | 2.6 % | 4239 | 428.7 | 0/3 |
| duplicates and reordering | 0 | 70 | 414.4 | 3/3 |
| 2 simultaneous inputs, reserve 140.8 kbit | 0 | 86 | 835.8 | 3/3 |
| 4 simultaneous inputs, reserve 281.6 kbit (over the ceiling: run with 184) | 99.0 % | 7528 | -1.0 | 0/3 |
| 1000 ms partition, replay, takeover | 0.2 % | 4841 | 413.8 | 0/3 |
| 2 hop routes, reserve 211.2 kbit (over the ceiling: run with 184) | 0 | 66 | -1.0 | 3/3 |
| 2 erased, PHY 5000 kbit/s | 0 | 91 | 419.9 | 3/3 |
| 2 erased, PHY 2000 kbit/s | 36.3 % | 199 | 433.3 | 0/3 |
| 2 erased, PHY 1000 kbit/s | 100.0 % | 2434 | 3086.3 | 0/3 |
| 2 erased, opportunities at 60 Hz | 0 | 96 | 418.5 | 3/3 |
| 2 erased, opportunities at 45 Hz | 46.5 % | 113 | 420.9 | 0/3 |

### unicast 200 B: 16 nodes, 2 copies, 0.5 input/s, state 5 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 60 | 303.0 | 3/3 |
| 1 erased | 0 | 71 | 303.0 | 3/3 |
| 1 erased, jitter 5 ms | 0 | 76 | 305.5 | 3/3 |
| 1 erased, jitter 10 ms | 0 | 80 | 308.0 | 3/3 |
| independent loss 0.01 | 4 of 41085 | 1472 | 305.8 | 1/3 |
| independent loss 0.03 | 37 of 41085 | 3015 | 312.3 | 0/3 |
| independent loss 0.05 | 0.3 % | 4300 | 320.0 | 0/3 |
| independent loss 0.10 | 1.0 % | 6508 | 345.0 | 0/3 |
| 1 erased, 1% loss, 5 ms jitter | 1.1 % | 3119 | 307.7 | 0/3 |
| 1 copy periods blocked | 7.6 % | 5231 | 308.6 | 0/3 |
| 2 copy periods blocked | 100.0 % | 20799 | 412.9 | 0/3 |
| 3 copy periods blocked | 100.0 % | 20228 | 415.3 | 0/3 |
| 5 copy periods blocked | 100.0 % | 20090 | 424.5 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 99.8 % | 7181 | 394.6 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 100.0 % | 63123 | 691.1 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 71683 | 1511.8 | 0/3 |
| 50 ms blackout every 10 s | 0.3 % | 3969 | 304.5 | 0/3 |
| 100 ms blackout every 10 s | 0.8 % | 4109 | 306.5 | 0/3 |
| 250 ms blackout every 10 s | 2.8 % | 4210 | 310.9 | 0/3 |
| duplicates and reordering | 0 | 74 | 303.2 | 3/3 |
| 2 simultaneous inputs, reserve 96 kbit | 26.7 % | 120 | 593.3 | 0/3 |
| 4 simultaneous inputs, reserve 192 kbit (over the ceiling: run with 184) | 63.3 % | 197 | -1.0 | 0/3 |
| 1000 ms partition, replay, takeover | 0.1 % | 2662 | 302.5 | 0/3 |
| 2 hop routes, reserve 96 kbit | 0 | 85 | 1655.4 | 3/3 |
| 1 erased, PHY 5000 kbit/s | 0 | 85 | 304.1 | 3/3 |
| 1 erased, PHY 2000 kbit/s | 100.0 % | 588 | 913.6 | 0/3 |
| 1 erased, PHY 1000 kbit/s | 100.0 % | 3598 | 4028.7 | 0/3 |
| 1 erased, opportunities at 60 Hz | 0 | 85 | 307.0 | 3/3 |
| 1 erased, opportunities at 45 Hz | 0 | 93 | 311.1 | 3/3 |

### unicast 200 B: 16 nodes, 3 copies, state 5 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 62 | 1045.4 | 3/3 |
| 2 erased | 0 | 87 | 1045.4 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 91 | 1048.0 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 94 | 1050.6 | 3/3 |
| independent loss 0.01 | 0 | 74 | 1049.2 | 3/3 |
| independent loss 0.03 | 4 of 82035 | 1787 | 1057.6 | 1/3 |
| independent loss 0.05 | 13 of 82035 | 3240 | 1066.9 | 0/3 |
| independent loss 0.10 | 0.1 % | 4993 | 1295.4 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 1.0 % | 4734 | 1057.3 | 0/3 |
| 1 copy periods blocked | 0 | 82 | 1049.3 | 3/3 |
| 2 copy periods blocked | 7.1 % | 24402 | 1503.8 | 0/3 |
| 3 copy periods blocked | 100.0 % | 98224 | 20826.2 | 0/3 |
| 5 copy periods blocked | 100.0 % | 96796 | 16076.6 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 6.6 % | 9112 | 1911.0 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 99.9 % | 99239 | 25877.0 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 99.9 % | 99711 | 29150.0 | 0/3 |
| 50 ms blackout every 10 s | 0.3 % | 4127 | 1048.6 | 0/3 |
| 100 ms blackout every 10 s | 1.1 % | 4643 | 1056.6 | 0/3 |
| 250 ms blackout every 10 s | 2.8 % | 4714 | 1080.7 | 0/3 |
| duplicates and reordering | 0 | 74 | 1045.5 | 3/3 |
| 2 simultaneous inputs, reserve 192 kbit (over the ceiling: run with 184) | 26.7 % | 120 | -1.0 | 0/3 |
| 4 simultaneous inputs, reserve 384 kbit (over the ceiling: run with 184) | 99.7 % | 36559 | -1.0 | 0/3 |
| 1000 ms partition, replay, takeover | 0.1 % | 5286 | 1046.6 | 0/3 |
| 2 hop routes, reserve 288 kbit (over the ceiling: run with 184) | 76 of 82035 | 144 | -1.0 | 0/3 |
| 2 erased, PHY 5000 kbit/s | 0.7 % | 138 | 1051.7 | 0/3 |
| 2 erased, PHY 2000 kbit/s | 82.8 % | 353 | 1154.9 | 0/3 |
| 2 erased, PHY 1000 kbit/s | 100.0 % | 19097 | 2027.9 | 0/3 |
| 2 erased, opportunities at 60 Hz | 53 of 82035 | 104 | 1055.6 | 1/3 |
| 2 erased, opportunities at 45 Hz | 49.1 % | 126 | 1062.7 | 0/3 |

### unicast 100 B: 8 nodes, 3 copies, state 15 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 54 | 69.9 | 3/3 |
| 2 erased | 0 | 77 | 69.9 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 81 | 72.2 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 86 | 75.2 | 3/3 |
| independent loss 0.01 | 0 | 75 | 70.0 | 3/3 |
| independent loss 0.03 | 0 | 76 | 70.1 | 3/3 |
| independent loss 0.05 | 2 of 19180 | 1867 | 70.4 | 1/3 |
| independent loss 0.10 | 0.1 % | 3788 | 87.7 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 0.9 % | 2812 | 72.3 | 0/3 |
| 1 copy periods blocked | 0 | 76 | 70.0 | 3/3 |
| 2 copy periods blocked | 3.7 % | 4993 | 70.1 | 0/3 |
| 3 copy periods blocked | 99.1 % | 6040 | 70.1 | 0/3 |
| 5 copy periods blocked | 100.0 % | 7964 | 70.4 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 3.7 % | 4993 | 70.1 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 98.6 % | 3492 | 70.1 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 642 | 70.2 | 0/3 |
| 50 ms blackout every 10 s | 0.4 % | 1996 | 69.9 | 0/3 |
| 100 ms blackout every 10 s | 0.9 % | 2110 | 69.9 | 0/3 |
| 250 ms blackout every 10 s | 2.4 % | 2263 | 70.0 | 0/3 |
| duplicates and reordering | 0 | 74 | 69.9 | 3/3 |
| 2 simultaneous inputs, reserve 44.8 kbit | 0 | 53 | 63.8 | 3/3 |
| 4 simultaneous inputs, reserve 89.6 kbit | 0 | 62 | 63.8 | 3/3 |
| 1000 ms partition, replay, takeover | 0.2 % | 2859 | 69.9 | 0/3 |
| 2 hop routes, reserve 67.2 kbit | 0 | 58 | 219.4 | 3/3 |
| 2 erased, PHY 5000 kbit/s | 0 | 78 | 70.4 | 3/3 |
| 2 erased, PHY 2000 kbit/s | 0 | 91 | 72.0 | 3/3 |
| 2 erased, PHY 1000 kbit/s | 16.8 % | 145 | 85.1 | 0/3 |
| 2 erased, opportunities at 60 Hz | 0 | 93 | 75.7 | 3/3 |
| 2 erased, opportunities at 45 Hz | 43.2 % | 110 | 78.7 | 0/3 |

### unicast 100 B: 12 nodes, 3 copies, state 15 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 56 | 104.0 | 3/3 |
| 2 erased | 0 | 78 | 104.0 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 82 | 106.6 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 87 | 109.4 | 3/3 |
| independent loss 0.01 | 0 | 74 | 105.0 | 3/3 |
| independent loss 0.03 | 1 of 45122 | 1904 | 107.0 | 2/3 |
| independent loss 0.05 | 5 of 45122 | 2024 | 110.0 | 0/3 |
| independent loss 0.10 | 0.1 % | 4288 | 117.3 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 0.9 % | 3116 | 107.4 | 0/3 |
| 1 copy periods blocked | 0 | 77 | 105.1 | 3/3 |
| 2 copy periods blocked | 4.4 % | 5911 | 106.3 | 0/3 |
| 3 copy periods blocked | 99.9 % | 11171 | 124.0 | 0/3 |
| 5 copy periods blocked | 100.0 % | 22269 | 126.3 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 6.1 % | 7013 | 123.3 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 99.9 % | 8054 | 139.2 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 10262 | 280.5 | 0/3 |
| 50 ms blackout every 10 s | 0.3 % | 2557 | 104.4 | 0/3 |
| 100 ms blackout every 10 s | 1.0 % | 2850 | 104.5 | 0/3 |
| 250 ms blackout every 10 s | 2.6 % | 3137 | 105.3 | 0/3 |
| duplicates and reordering | 0 | 72 | 104.3 | 3/3 |
| 2 simultaneous inputs, reserve 70.4 kbit | 0 | 65 | 122.4 | 3/3 |
| 4 simultaneous inputs, reserve 140.8 kbit | 0 | 86 | 531.2 | 3/3 |
| 1000 ms partition, replay, takeover | 0.1 % | 3199 | 104.1 | 0/3 |
| 2 hop routes, reserve 105.6 kbit | 0 | 63 | 764.7 | 3/3 |
| 2 erased, PHY 5000 kbit/s | 0 | 82 | 105.6 | 3/3 |
| 2 erased, PHY 2000 kbit/s | 19.7 % | 146 | 122.6 | 0/3 |
| 2 erased, PHY 1000 kbit/s | 100.0 % | 1900 | 1674.3 | 0/3 |
| 2 erased, opportunities at 60 Hz | 0 | 94 | 107.7 | 3/3 |
| 2 erased, opportunities at 45 Hz | 44.0 % | 111 | 108.1 | 0/3 |

### unicast 100 B: 16 nodes, 3 copies, state 10 Hz

| test | late or missing | worst L ms | state age P95 ms | service |
|---|---|---|---|---|
| clean | 0 | 60 | 184.6 | 3/3 |
| 2 erased | 0 | 82 | 184.6 | 3/3 |
| 2 erased, jitter 5 ms | 0 | 87 | 187.1 | 3/3 |
| 2 erased, jitter 10 ms | 0 | 92 | 189.6 | 3/3 |
| independent loss 0.01 | 0 | 76 | 186.0 | 3/3 |
| independent loss 0.03 | 4 of 82035 | 2012 | 188.8 | 1/3 |
| independent loss 0.05 | 9 of 82035 | 4860 | 193.5 | 0/3 |
| independent loss 0.10 | 0.1 % | 4755 | 209.3 | 0/3 |
| 2 erased, 1% loss, 5 ms jitter | 1.1 % | 3184 | 188.6 | 0/3 |
| 1 copy periods blocked | 0 | 82 | 186.1 | 3/3 |
| 2 copy periods blocked | 12.8 % | 14112 | 189.5 | 0/3 |
| 3 copy periods blocked | 100.0 % | 25945 | 227.4 | 0/3 |
| 5 copy periods blocked | 100.0 % | 33787 | 233.1 | 0/3 |
| 2 copy periods blocked, repair share 16 kbit | 12.5 % | 6038 | 212.1 | 0/3 |
| 3 copy periods blocked, control fixture 2 Hz | 100.0 % | 37359 | 311.6 | 0/3 |
| 3 copy periods blocked, control fixture 7.5 Hz | 100.0 % | 55917 | 651.0 | 0/3 |
| 50 ms blackout every 10 s | 0.3 % | 3093 | 185.2 | 0/3 |
| 100 ms blackout every 10 s | 1.1 % | 3181 | 186.0 | 0/3 |
| 250 ms blackout every 10 s | 2.8 % | 3285 | 187.4 | 0/3 |
| duplicates and reordering | 0 | 74 | 184.8 | 3/3 |
| 2 simultaneous inputs, reserve 96 kbit | 0 | 82 | 278.3 | 3/3 |
| 4 simultaneous inputs, reserve 192 kbit (over the ceiling: run with 184) | 26.7 % | 120 | -1.0 | 0/3 |
| 1000 ms partition, replay, takeover | 0.1 % | 3836 | 184.7 | 0/3 |
| 2 hop routes, reserve 144 kbit | 0 | 80 | 1725.6 | 3/3 |
| 2 erased, PHY 5000 kbit/s | 0 | 92 | 185.0 | 3/3 |
| 2 erased, PHY 2000 kbit/s | 99.9 % | 512 | 587.3 | 0/3 |
| 2 erased, PHY 1000 kbit/s | 100.0 % | 3385 | 3425.6 | 0/3 |
| 2 erased, opportunities at 60 Hz | 0 | 99 | 186.5 | 3/3 |
| 2 erased, opportunities at 45 Hz | 45.1 % | 114 | 178.9 | 0/3 |
