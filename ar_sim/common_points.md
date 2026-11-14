# 16 participants, common delivery, measured CoT sizes, human-limited input

| test | CoT XML, lean schema (712 B) | TAK v1, lean schema (556 B) | XML + session dictionary, as specified (234 B) |
|---|---|---|---|
| two erasures, human renewal input | on time; worst L 77 ms; state 100 ms; 68 kbps; airtime 11 % | on time; worst L 76 ms; state 99 ms; 50 kbps; airtime 8 % | on time; worst L 76 ms; state 99 ms; 20 kbps; airtime 3 % |
| phase sweep, two erasures, periodic input | 432 of 432; worst L 77 ms | 432 of 432; worst L 77 ms | 432 of 432; worst L 76 ms |
| all 16 fire 7-pull strings in lockstep, 154 ms splits, two erasures | on time; worst L 88 ms; state 96 ms; 68 kbps; airtime 11 % | on time; worst L 82 ms; state 94 ms; 51 kbps; airtime 8 % | on time; worst L 78 ms; state 91 ms; 20 kbps; airtime 3 % |
| all 16 fire 10-pull strings in lockstep, 100 ms splits, two erasures | 20.0 % late; worst L 253 ms; state 96 ms; 68 kbps; airtime 11 % | on time; worst L 73 ms; state 94 ms; 51 kbps; airtime 8 % | on time; worst L 69 ms; state 91 ms; 20 kbps; airtime 3 % |
| jitter 10 ms, two erasures | on time; worst L 86 ms; state 105 ms; 68 kbps; airtime 11 % | on time; worst L 86 ms; state 105 ms; 50 kbps; airtime 8 % | on time; worst L 86 ms; state 105 ms; 20 kbps; airtime 3 % |
| independent loss 1 % | on time; worst L 76 ms; state 100 ms; 68 kbps; airtime 11 % | on time; worst L 76 ms; state 100 ms; 50 kbps; airtime 8 % | on time; worst L 76 ms; state 100 ms; 20 kbps; airtime 3 % |
| independent loss 3 % | 1 of 39345 late; worst L 1720 ms; state 100 ms; 68 kbps; airtime 11 % | 1 of 39345 late; worst L 1720 ms; state 100 ms; 50 kbps; airtime 8 % | 1 of 39345 late; worst L 1720 ms; state 100 ms; 20 kbps; airtime 3 % |
| two erasures, 1 % loss, 5 ms jitter | 1.0 % late; worst L 3088 ms; state 102 ms; 69 kbps; airtime 11 % | 1.0 % late; worst L 3088 ms; state 102 ms; 51 kbps; airtime 8 % | 1.0 % late; worst L 3088 ms; state 102 ms; 20 kbps; airtime 3 % |
| blockage, 1 copy period | on time; worst L 76 ms; state 100 ms; 68 kbps; airtime 11 % | on time; worst L 76 ms; state 100 ms; 50 kbps; airtime 8 % | on time; worst L 76 ms; state 100 ms; 20 kbps; airtime 3 % |
| blockage, 2 copy periods | 12.0 % late; worst L 36254 ms; state 187 ms; 85 kbps; airtime 14 % | 8.8 % late; worst L 5861 ms; state 100 ms; 58 kbps; airtime 10 % | 1.8 % late; worst L 3962 ms; state 100 ms; 20 kbps; airtime 3 % |
| blockage, 3 copy periods | 100.0 % late; worst L 53302 ms; state 440 ms; 91 kbps; airtime 15 % | 100.0 % late; worst L 34112 ms; state 213 ms; 109 kbps; airtime 17 % | 99.6 % late; worst L 4955 ms; state 100 ms; 51 kbps; airtime 8 % |
| blackout 50 ms every 10 s | 0.3 % late; worst L 4868 ms; state 100 ms; 68 kbps; airtime 11 % | 0.3 % late; worst L 1856 ms; state 100 ms; 51 kbps; airtime 8 % | 0.3 % late; worst L 1854 ms; state 99 ms; 20 kbps; airtime 3 % |
| duplicates and reordering | on time; worst L 73 ms; state 100 ms; 68 kbps; airtime 11 % | on time; worst L 73 ms; state 100 ms; 50 kbps; airtime 8 % | on time; worst L 73 ms; state 99 ms; 20 kbps; airtime 3 % |
| partition 1 s, replay, takeover | 0.2 % late; worst L 6248 ms; state 100 ms; 68 kbps; airtime 11 % | 0.2 % late; worst L 3437 ms; state 99 ms; 50 kbps; airtime 8 % | 0.2 % late; worst L 2991 ms; state 99 ms; 20 kbps; airtime 3 % |
| PHY 5 Mbit/s, two erasures | on time; worst L 78 ms; state 100 ms; 68 kbps; airtime 22 % | on time; worst L 77 ms; state 100 ms; 50 kbps; airtime 16 % | on time; worst L 76 ms; state 99 ms; 20 kbps; airtime 6 % |
| PHY 2 Mbit/s, two erasures | on time; worst L 98 ms; state 104 ms; 68 kbps; airtime 54 % | on time; worst L 87 ms; state 102 ms; 50 kbps; airtime 40 % | on time; worst L 77 ms; state 100 ms; 20 kbps; airtime 16 % |
