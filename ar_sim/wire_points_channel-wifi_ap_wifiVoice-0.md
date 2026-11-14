# 16 participants, common delivery, encoded-wire CoT, serverless alignment (channel=wifi_ap wifiVoice=0)

Urgent reserve per point: 7 inputs x 3 copies at the largest encoded event in a probe run (+8 B): xml_lean 613 B payload, 112392 bits; tak_lean 457 B payload, 86184 bits

As specified, neither schema is admissible at 16 participants: CoT XML, as specified reserves 215544 of the 200000-bit ledger for urgent copies alone (1227 B events, over the 1200 B datagram with authentication); TAK v1, as specified reserves 179928 of the 200000-bit ledger for urgent copies alone (1015 B events), leaving too little for 10 Hz state.

| test | CoT XML, lean schema | TAK v1, lean schema |
|---|---|---|
| two erasures, human renewal input | 1.7 % late; worst L 6001 ms; state 103 ms; 79 kbps; airtime 25 % (profile 0/3) | 1.3 % late; worst L 3880 ms; state 101 ms; 58 kbps; airtime 19 % (profile 0/3) |
| phase sweep, two erasures, periodic input | 0 of 432 phase points pass; worst L 6667 ms (profile 0/432) | 0 of 432 phase points pass; worst L 3853 ms (profile 0/432) |
| all 16 fire 7-pull strings in lockstep, 154 ms splits, two erasures | 75.1 % late; worst L 12351 ms; state 418 ms; 104 kbps; airtime 21 % (profile 0/3) | 60.8 % late; worst L 6603 ms; state 187 ms; 108 kbps; airtime 20 % (profile 0/3) |
| all 16 fire 10-pull strings in lockstep, 100 ms splits, two erasures | 84.3 % late; worst L 13821 ms; state 548 ms; 104 kbps; airtime 20 % (profile 0/3) | 69.2 % late; worst L 9066 ms; state 190 ms; 107 kbps; airtime 20 % (profile 0/3) |
| jitter 10 ms, two erasures | 1.4 % late; worst L 4974 ms; state 109 ms; 78 kbps; airtime 25 % (profile 0/3) | 1.6 % late; worst L 3059 ms; state 107 ms; 58 kbps; airtime 19 % (profile 0/3) |
| independent loss 1 % | on time; worst L 78 ms; state 102 ms; 76 kbps; airtime 25 % | 2 of 39345 late; worst L 1995 ms; state 101 ms; 56 kbps; airtime 19 % (profile 1/3) |
| independent loss 3 % | on time; worst L 83 ms; state 102 ms; 76 kbps; airtime 25 % | 5 of 39345 late; worst L 1967 ms; state 101 ms; 56 kbps; airtime 19 % (profile 0/3) |
| two erasures, 1 % loss, 5 ms jitter | 3.0 % late; worst L 6246 ms; state 108 ms; 81 kbps; airtime 25 % (profile 0/3) | 2.5 % late; worst L 4082 ms; state 104 ms; 59 kbps; airtime 19 % (profile 0/3) |
| blockage, 1 copy period | 0.3 % late; worst L 5116 ms; state 101 ms; 76 kbps; airtime 25 % (profile 0/3) | 0.8 % late; worst L 4090 ms; state 101 ms; 57 kbps; airtime 19 % (profile 0/3) |
| blockage, 2 copy periods | 51.5 % late; worst L 48217 ms; state 988 ms; 104 kbps; airtime 19 % (profile 0/3) | 49.3 % late; worst L 14058 ms; state 310 ms; 106 kbps; airtime 22 % (profile 0/3) |
| blockage, 3 copy periods | 100.0 % late; worst L 53756 ms; state 981 ms; 104 kbps; airtime 19 % (profile 0/3) | 99.9 % late; worst L 39720 ms; state 934 ms; 122 kbps; airtime 21 % (profile 0/3) |
| blackout 50 ms every 10 s | 0.4 % late; worst L 4450 ms; state 101 ms; 77 kbps; airtime 25 % (profile 1/3) | 0.3 % late; worst L 2845 ms; state 101 ms; 57 kbps; airtime 19 % (profile 1/3) |
| duplicates and reordering | on time; worst L 75 ms; state 102 ms; 76 kbps; airtime 25 % (profile 0/3) | on time; worst L 73 ms; state 101 ms; 56 kbps; airtime 19 % (profile 1/3) |
| partition 1 s, replay, takeover | 0.2 % late; worst L 5226 ms; state 101 ms; 77 kbps; airtime 25 % (profile 0/3) | 0.2 % late; worst L 3570 ms; state 101 ms; 56 kbps; airtime 19 % (profile 0/3) |
| PHY 5 Mbit/s, two erasures | 1.7 % late; worst L 6001 ms; state 103 ms; 79 kbps; airtime 25 % (profile 0/3) | 1.3 % late; worst L 3880 ms; state 101 ms; 58 kbps; airtime 19 % (profile 0/3) |
| PHY 2 Mbit/s, two erasures | 1.7 % late; worst L 6001 ms; state 103 ms; 79 kbps; airtime 25 % (profile 0/3) | 1.3 % late; worst L 3880 ms; state 101 ms; 58 kbps; airtime 19 % (profile 0/3) |

Alignment and calibration (two erasures, human renewal input; P95 over three seeds, worst seed):

| point | control kbps | avatar near P95 m | avatar far P95 m | marker near P95 m | marker far P95 m |
|---|---|---|---|---|---|
| CoT XML, lean schema | 12.9 | 0.255 | 2.169 | 0.315 | 2.198 |
| TAK v1, lean schema | 7.4 | 0.254 | 2.226 | 0.331 | 2.36 |
