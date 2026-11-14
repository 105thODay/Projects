# Player-count limits, common delivery, lean CoT XML, serverless alignment (Rev 6)

Largest count with every obligation on time in three 30 s seeds; in parentheses, what happens at the next rung.

## Q0: only the channel loses frames

| channel | normal | together | strings | together, own clocks | strings, own clocks |
|---|---|---|---|---|---|
| ideal | **128** (96.5 % late at 144, airtime 97 %) | **40** (1.2 % late at 48, airtime 33 %) | **40** (0.1 % late at 48, airtime 33 %) | **96** (12.3 % late at 112, airtime 76 %) | **64** (80.8 % late at 80, airtime 56 %) |
| basic/vo | **16** (0.1 % late at 20, 6 % of attempts collide, busy 30 %) | **2** (0.7 % late at 4, 32 % of attempts collide, busy 6 %) | fails at 2 (7.7 % late at 2, 18 % of attempts collide, busy 3 %) | **6** (0.2 % late at 8, 13 % of attempts collide, busy 12 %) | **6** (8.2 % late at 8, 9 % of attempts collide, busy 12 %) |
| basic/be | **32** (0.0 % late at 40, 16 % of attempts collide, busy 58 %) | **4** (0.2 % late at 6, 26 % of attempts collide, busy 9 %) | **2** (5.4 % late at 4, 20 % of attempts collide, busy 6 %) | **8** (0.4 % late at 10, 11 % of attempts collide, busy 15 %) | **4** (0.0 % late at 6, 3 % of attempts collide, busy 10 %) |
| unicast/vo | **14** (0.1 % late at 16, 42 % of attempts collide, busy 55 %) | **4** (0.0 % late at 6, 67 % of attempts collide, busy 8 %) | fails at 2 (11.9 % late at 2, 20 % of attempts collide, busy 1 %) | **8** (0.3 % late at 10, 50 % of attempts collide, busy 23 %) | **4** (5.2 % late at 6, 12 % of attempts collide, busy 9 %) |
| basic/doze | fails at 2 (48.1 % late at 2, 1 % of attempts collide, busy 3 %) | fails at 2 (49.3 % late at 2, 13 % of attempts collide, busy 3 %) | fails at 2 (58.3 % late at 2, 13 % of attempts collide, busy 3 %) | fails at 2 (48.6 % late at 2, 2 % of attempts collide, busy 3 %) | fails at 2 (57.1 % late at 2, 1 % of attempts collide, busy 3 %) |
| unicast/doze | fails at 2 (23.7 % late at 2, 0 % of attempts collide, busy 1 %) | fails at 2 (20.8 % late at 2, 17 % of attempts collide, busy 1 %) | fails at 2 (29.8 % late at 2, 15 % of attempts collide, busy 1 %) | fails at 2 (20.8 % late at 2, 0 % of attempts collide, busy 1 %) | fails at 2 (26.2 % late at 2, 1 % of attempts collide, busy 1 %) |

## Q2: two erasures on every urgent event (the profile's bound)

| channel | normal | together | strings | together, own clocks | strings, own clocks |
|---|---|---|---|---|---|
| ideal | **112** (0.1 % late at 128, airtime 87 %) | **24** (16.3 % late at 28, airtime 19 %) | **20** (1.8 % late at 24, airtime 17 %) | **28** (2.0 % late at 32, airtime 22 %) | **28** (1.6 % late at 32, airtime 22 %) |
| basic/vo | **2** (0.4 % late at 4, 3 % of attempts collide, busy 6 %) | fails at 2 (9.0 % late at 2, 18 % of attempts collide, busy 3 %) | fails at 2 (22.6 % late at 2, 18 % of attempts collide, busy 3 %) | **4** (0.9 % late at 6, 2 % of attempts collide, busy 9 %) | **2** (0.3 % late at 4, 1 % of attempts collide, busy 6 %) |
| basic/be | fails at 2 (0.7 % late at 2, 1 % of attempts collide, busy 3 %) | fails at 2 (1.4 % late at 2, 11 % of attempts collide, busy 3 %) | fails at 2 (7.7 % late at 2, 11 % of attempts collide, busy 3 %) | **2** (0.7 % late at 4, 2 % of attempts collide, busy 6 %) | fails at 2 (1.2 % late at 2, 2 % of attempts collide, busy 3 %) |
| unicast/vo | **14** (0.2 % late at 16, 43 % of attempts collide, busy 55 %) | **4** (2.1 % late at 6, 67 % of attempts collide, busy 8 %) | fails at 2 (21.4 % late at 2, 20 % of attempts collide, busy 1 %) | **6** (2.8 % late at 8, 40 % of attempts collide, busy 16 %) | **4** (8.5 % late at 6, 12 % of attempts collide, busy 9 %) |
| basic/doze | fails at 2 (77.8 % late at 2, 1 % of attempts collide, busy 3 %) | fails at 2 (70.1 % late at 2, 13 % of attempts collide, busy 3 %) | fails at 2 (78.6 % late at 2, 13 % of attempts collide, busy 3 %) | fails at 2 (76.4 % late at 2, 2 % of attempts collide, busy 3 %) | fails at 2 (75.0 % late at 2, 1 % of attempts collide, busy 3 %) |
| unicast/doze | fails at 2 (32.6 % late at 2, 0 % of attempts collide, busy 1 %) | fails at 2 (29.9 % late at 2, 17 % of attempts collide, busy 1 %) | fails at 2 (45.2 % late at 2, 15 % of attempts collide, busy 1 %) | fails at 2 (32.6 % late at 2, 0 % of attempts collide, busy 1 %) | fails at 2 (37.5 % late at 2, 1 % of attempts collide, busy 1 %) |
