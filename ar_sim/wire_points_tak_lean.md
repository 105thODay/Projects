# 16 participants, common delivery, encoded-wire CoT, serverless alignment; forms tak_lean

Urgent reserve per point: 7 inputs x 3 copies at the largest encoded event in a probe run (+8 B): tak_lean 457 B payload, 86184 bits

As specified, neither schema is admissible at 16 participants: TAK v1, as specified reserves 179928 of the 200000-bit ledger for urgent copies alone (1015 B events), leaving too little for 10 Hz state.

| test | TAK v1, lean schema |
|---|---|
| two erasures, human renewal input | on time; worst L 76 ms; state 99 ms; 50 kbps; airtime 8 % |
| phase sweep, two erasures, periodic input | 432 of 432; worst L 77 ms |
| all 16 fire 7-pull strings in lockstep, 154 ms splits, two erasures | on time; worst L 82 ms; state 94 ms; 50 kbps; airtime 8 % |
| all 16 fire 10-pull strings in lockstep, 100 ms splits, two erasures | on time; worst L 72 ms; state 94 ms; 50 kbps; airtime 8 % |
| jitter 10 ms, two erasures | on time; worst L 86 ms; state 105 ms; 50 kbps; airtime 8 % |
| independent loss 1 % | on time; worst L 74 ms; state 100 ms; 49 kbps; airtime 8 % |
| independent loss 3 % | 3 of 39345 late; worst L 1313 ms; state 100 ms; 50 kbps; airtime 8 % (profile 1/3) |
| two erasures, 1 % loss, 5 ms jitter | 0.9 % late; worst L 2917 ms; state 102 ms; 50 kbps; airtime 8 % (profile 0/3) |
| blockage, 1 copy period | on time; worst L 76 ms; state 100 ms; 50 kbps; airtime 8 % |
| blockage, 2 copy periods | 8.9 % late; worst L 6090 ms; state 100 ms; 56 kbps; airtime 10 % (profile 0/3) |
| blockage, 3 copy periods | 100.0 % late; worst L 27990 ms; state 226 ms; 113 kbps; airtime 18 % (profile 0/3) |
| blackout 50 ms every 10 s | 0.3 % late; worst L 1856 ms; state 100 ms; 50 kbps; airtime 8 % (profile 1/3) |
| duplicates and reordering | on time; worst L 74 ms; state 100 ms; 50 kbps; airtime 8 % (profile 2/3) |
| partition 1 s, replay, takeover | 0.2 % late; worst L 2992 ms; state 100 ms; 50 kbps; airtime 8 % (profile 0/3) |
| PHY 5 Mbit/s, two erasures | on time; worst L 77 ms; state 100 ms; 50 kbps; airtime 16 % |
| PHY 2 Mbit/s, two erasures | on time; worst L 85 ms; state 102 ms; 50 kbps; airtime 40 % |

Alignment and calibration (two erasures, human renewal input; P95 over three seeds, worst seed):

| point | control kbps | avatar near P95 m | avatar far P95 m | marker near P95 m | marker far P95 m |
|---|---|---|---|---|---|
| TAK v1, lean schema | 6.0 | 0.293 | 2.207 | 0.304 | 2.32 |
