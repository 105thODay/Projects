#!/bin/sh
# Networked AR profile: every case and sweep. Output: ar_matrix_tak.txt (then: python3 tools/summarize_matrix.py).
exec > ar_matrix_tak.txt
echo '## gate | analytic | analytic'; ./ar_sim analytic
echo '## gate | F0 | F0'; ./ar_sim F0
echo '## qualification | Q0 | Q0 10 wire=tak_lean align=1'; ./ar_sim Q0 10 wire=tak_lean align=1
echo '## qualification | Q0 phase sweep | phase Q0 wire=tak_lean align=1'; ./ar_sim phase Q0 wire=tak_lean align=1
echo '## qualification | Q1 | Q1 10 wire=tak_lean align=1'; ./ar_sim Q1 10 wire=tak_lean align=1
echo '## qualification | Q1 phase sweep | phase Q1 wire=tak_lean align=1'; ./ar_sim phase Q1 wire=tak_lean align=1
echo '## qualification | Q2 | Q2 10 wire=tak_lean align=1'; ./ar_sim Q2 10 wire=tak_lean align=1
echo '## qualification | Q2 phase sweep | phase Q2 wire=tak_lean align=1'; ./ar_sim phase Q2 wire=tak_lean align=1
echo '## qualification | Q2 phase sweep, D at its 20 ms limit | phase Q2 wire=tak_lean align=1 propagationUs=18860'; ./ar_sim phase Q2 wire=tak_lean align=1 propagationUs=18860
echo '## R0 independent loss | loss 0 both ways | R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=0'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=0
echo '## R0 independent loss | loss 0 A to B | R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=1'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=1
echo '## R0 independent loss | loss 0 B to A | R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=2'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0 lossDirection=2
echo '## R0 independent loss | loss 0.01 both ways | R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=0'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=0
echo '## R0 independent loss | loss 0.01 A to B | R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=1'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=1
echo '## R0 independent loss | loss 0.01 B to A | R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=2'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.01 lossDirection=2
echo '## R0 independent loss | loss 0.03 both ways | R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=0'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=0
echo '## R0 independent loss | loss 0.03 A to B | R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=1'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=1
echo '## R0 independent loss | loss 0.03 B to A | R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=2'; ./ar_sim R0 10 wire=tak_lean align=1 lossProb=0.03 lossDirection=2
echo '## J0 jitter | jitter 0 ms, 0 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=0'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=0
echo '## J0 jitter | jitter 0 ms, 1 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=1'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=1
echo '## J0 jitter | jitter 0 ms, 2 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=2'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=0 eraseCopies=2
echo '## J0 jitter | jitter 5 ms, 0 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=0'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=0
echo '## J0 jitter | jitter 5 ms, 1 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=1'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=1
echo '## J0 jitter | jitter 5 ms, 2 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=2'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=5000 eraseCopies=2
echo '## J0 jitter | jitter 10 ms, 0 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=0'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=0
echo '## J0 jitter | jitter 10 ms, 1 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=1'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=1
echo '## J0 jitter | jitter 10 ms, 2 erased | J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=2'; ./ar_sim J0 3 wire=tak_lean align=1 jitterMaxUs=10000 eraseCopies=2
echo '## C0 shared blockage | 1 copy periods blocked | C0 3 wire=tak_lean align=1 blockPeriods=1'; ./ar_sim C0 3 wire=tak_lean align=1 blockPeriods=1
echo '## C0 shared blockage | 2 copy periods blocked | C0 3 wire=tak_lean align=1 blockPeriods=2'; ./ar_sim C0 3 wire=tak_lean align=1 blockPeriods=2
echo '## C0 shared blockage | 3 copy periods blocked | C0 3 wire=tak_lean align=1 blockPeriods=3'; ./ar_sim C0 3 wire=tak_lean align=1 blockPeriods=3
echo '## C0 shared blockage | 5 copy periods blocked | C0 3 wire=tak_lean align=1 blockPeriods=5'; ./ar_sim C0 3 wire=tak_lean align=1 blockPeriods=5
echo '## S0 severe loss | loss 0.05 | S0 10 wire=tak_lean align=1 lossProb=0.05 blackoutUs=0'; ./ar_sim S0 10 wire=tak_lean align=1 lossProb=0.05 blackoutUs=0
echo '## S0 severe loss | loss 0.10 | S0 10 wire=tak_lean align=1 lossProb=0.10 blackoutUs=0'; ./ar_sim S0 10 wire=tak_lean align=1 lossProb=0.10 blackoutUs=0
echo '## S0 blackout | 50 ms blackout every 10 s | S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=50000'; ./ar_sim S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=50000
echo '## S0 blackout | 100 ms blackout every 10 s | S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=100000'; ./ar_sim S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=100000
echo '## S0 blackout | 250 ms blackout every 10 s | S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=250000'; ./ar_sim S0 10 wire=tak_lean align=1 lossProb=0 blackoutUs=250000
echo '## P0 duplicate and reorder | reorder delay 5 ms | P0 3 wire=tak_lean align=1 reorderUs=5000'; ./ar_sim P0 3 wire=tak_lean align=1 reorderUs=5000
echo '## P0 duplicate and reorder | reorder delay 20 ms | P0 3 wire=tak_lean align=1 reorderUs=20000'; ./ar_sim P0 3 wire=tak_lean align=1 reorderUs=20000
echo '## O0 simultaneous inputs | burst 1, lending as written | O0 3 wire=tak_lean align=1 burst=1'; ./ar_sim O0 3 wire=tak_lean align=1 burst=1
echo '## O0 simultaneous inputs | burst 2, lending as written | O0 3 wire=tak_lean align=1 burst=2'; ./ar_sim O0 3 wire=tak_lean align=1 burst=2
echo '## O0 simultaneous inputs | burst 4, lending as written | O0 3 wire=tak_lean align=1 burst=4'; ./ar_sim O0 3 wire=tak_lean align=1 burst=4
echo '## O0 simultaneous inputs | burst 4, standing urgent reserve 64 kbit | O0 3 wire=tak_lean align=1 burst=4 lendUrgentShare=0'; ./ar_sim O0 3 wire=tak_lean align=1 burst=4 lendUrgentShare=0
echo '## O0 simultaneous inputs | burst 4, standing urgent reserve 72 kbit | O0 3 wire=tak_lean align=1 burst=4 lendUrgentShare=0 urgentShareBits=72000'; ./ar_sim O0 3 wire=tak_lean align=1 burst=4 lendUrgentShare=0 urgentShareBits=72000
echo '## R1 partition and rejoin | 1000 ms partition, replay, takeover | R1 3 wire=tak_lean align=1'; ./ar_sim R1 3 wire=tak_lean align=1
echo '## sweep input rate | 0.5 inputs/s | Q0 3 wire=tak_lean align=1 inputHz=0.5'; ./ar_sim Q0 3 wire=tak_lean align=1 inputHz=0.5
echo '## sweep input rate | 1 inputs/s | Q0 3 wire=tak_lean align=1 inputHz=1'; ./ar_sim Q0 3 wire=tak_lean align=1 inputHz=1
echo '## sweep input rate | 3 inputs/s | Q0 3 wire=tak_lean align=1 inputHz=3'; ./ar_sim Q0 3 wire=tak_lean align=1 inputHz=3
echo '## sweep routine state | 10 Hz, TAK v1 lean state | Q0 3 wire=tak_lean align=1 stateHz=10'; ./ar_sim Q0 3 wire=tak_lean align=1 stateHz=10
echo '## sweep routine state | 15 Hz, TAK v1 lean state | Q0 3 wire=tak_lean align=1 stateHz=15'; ./ar_sim Q0 3 wire=tak_lean align=1 stateHz=15
echo '## sweep routine state | 30 Hz, TAK v1 lean state | Q0 3 wire=tak_lean align=1 stateHz=30'; ./ar_sim Q0 3 wire=tak_lean align=1 stateHz=30
echo '## sweep opportunity rate | 30 Hz, 0 erased | Q0 3 wire=tak_lean align=1 opportunityHz=30'; ./ar_sim Q0 3 wire=tak_lean align=1 opportunityHz=30
echo '## sweep opportunity rate | 30 Hz, 1 erased | Q1 3 wire=tak_lean align=1 opportunityHz=30'; ./ar_sim Q1 3 wire=tak_lean align=1 opportunityHz=30
echo '## sweep opportunity rate | 30 Hz, 2 erased | Q2 3 wire=tak_lean align=1 opportunityHz=30'; ./ar_sim Q2 3 wire=tak_lean align=1 opportunityHz=30
echo '## sweep opportunity rate | 45 Hz, 0 erased | Q0 3 wire=tak_lean align=1 opportunityHz=45'; ./ar_sim Q0 3 wire=tak_lean align=1 opportunityHz=45
echo '## sweep opportunity rate | 45 Hz, 1 erased | Q1 3 wire=tak_lean align=1 opportunityHz=45'; ./ar_sim Q1 3 wire=tak_lean align=1 opportunityHz=45
echo '## sweep opportunity rate | 45 Hz, 2 erased | Q2 3 wire=tak_lean align=1 opportunityHz=45'; ./ar_sim Q2 3 wire=tak_lean align=1 opportunityHz=45
echo '## sweep opportunity rate | 60 Hz, 0 erased | Q0 3 wire=tak_lean align=1 opportunityHz=60'; ./ar_sim Q0 3 wire=tak_lean align=1 opportunityHz=60
echo '## sweep opportunity rate | 60 Hz, 1 erased | Q1 3 wire=tak_lean align=1 opportunityHz=60'; ./ar_sim Q1 3 wire=tak_lean align=1 opportunityHz=60
echo '## sweep opportunity rate | 60 Hz, 2 erased | Q2 3 wire=tak_lean align=1 opportunityHz=60'; ./ar_sim Q2 3 wire=tak_lean align=1 opportunityHz=60
echo '## sweep opportunity rate | 90 Hz, 0 erased | Q0 3 wire=tak_lean align=1 opportunityHz=90'; ./ar_sim Q0 3 wire=tak_lean align=1 opportunityHz=90
echo '## sweep opportunity rate | 90 Hz, 1 erased | Q1 3 wire=tak_lean align=1 opportunityHz=90'; ./ar_sim Q1 3 wire=tak_lean align=1 opportunityHz=90
echo '## sweep opportunity rate | 90 Hz, 2 erased | Q2 3 wire=tak_lean align=1 opportunityHz=90'; ./ar_sim Q2 3 wire=tak_lean align=1 opportunityHz=90
echo '## sweep PHY rate | 10000 kbit/s, 2 erased | Q2 3 wire=tak_lean align=1 phyBps=10000000'; ./ar_sim Q2 3 wire=tak_lean align=1 phyBps=10000000
echo '## sweep PHY rate | 2000 kbit/s, 2 erased | Q2 3 wire=tak_lean align=1 phyBps=2000000'; ./ar_sim Q2 3 wire=tak_lean align=1 phyBps=2000000
echo '## sweep PHY rate | 1000 kbit/s, 2 erased | Q2 3 wire=tak_lean align=1 phyBps=1000000'; ./ar_sim Q2 3 wire=tak_lean align=1 phyBps=1000000
echo '## sweep PHY rate | 500 kbit/s, 2 erased | Q2 3 wire=tak_lean align=1 phyBps=500000'; ./ar_sim Q2 3 wire=tak_lean align=1 phyBps=500000
echo '## sweep PHY rate | 200 kbit/s, 2 erased | Q2 3 wire=tak_lean align=1 phyBps=200000'; ./ar_sim Q2 3 wire=tak_lean align=1 phyBps=200000
echo '## participants | 2 nodes, unicast, lending as written | Q0 3 wire=tak_lean align=1 nodes=2'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=2
echo '## participants | 2 nodes, unicast, standing urgent reserve 64 kbit | Q0 3 wire=tak_lean align=1 nodes=2 lendUrgentShare=0 urgentShareBits=64000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=2 lendUrgentShare=0 urgentShareBits=64000
echo '## participants | 2 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=2 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=2 fanout=common
echo '## participants | 3 nodes, unicast, lending as written | Q0 3 wire=tak_lean align=1 nodes=3'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=3
echo '## participants | 3 nodes, unicast, standing urgent reserve 64 kbit | Q0 3 wire=tak_lean align=1 nodes=3 lendUrgentShare=0 urgentShareBits=64000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=3 lendUrgentShare=0 urgentShareBits=64000
echo '## participants | 3 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=3 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=3 fanout=common
echo '## participants | 4 nodes, unicast, lending as written | Q0 3 wire=tak_lean align=1 nodes=4'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4
echo '## participants | 4 nodes, unicast, standing urgent reserve 64 kbit | Q0 3 wire=tak_lean align=1 nodes=4 lendUrgentShare=0 urgentShareBits=64000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 lendUrgentShare=0 urgentShareBits=64000
echo '## participants | 4 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=4 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 fanout=common
echo '## participants | 8 nodes, unicast, lending as written | Q0 3 wire=tak_lean align=1 nodes=8'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=8
echo '## participants | 8 nodes, unicast, standing urgent reserve 120 kbit | Q0 3 wire=tak_lean align=1 nodes=8 lendUrgentShare=0 urgentShareBits=120000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=8 lendUrgentShare=0 urgentShareBits=120000
echo '## participants | 8 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=8 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=8 fanout=common
echo '## participants | 16 nodes, unicast, lending as written | Q0 3 wire=tak_lean align=1 nodes=16'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16
echo '## participants | 16 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=16 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16 fanout=common
echo '## participants | 32 nodes, common delivery | Q0 3 wire=tak_lean align=1 nodes=32 fanout=common'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=32 fanout=common
echo '## participants | 8 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | Q2 3 wire=tak_lean align=1 nodes=8 fanout=common lossProb=0.01 jitterMaxUs=5000'; ./ar_sim Q2 3 wire=tak_lean align=1 nodes=8 fanout=common lossProb=0.01 jitterMaxUs=5000
echo '## participants | 16 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | Q2 3 wire=tak_lean align=1 nodes=16 fanout=common lossProb=0.01 jitterMaxUs=5000'; ./ar_sim Q2 3 wire=tak_lean align=1 nodes=16 fanout=common lossProb=0.01 jitterMaxUs=5000
echo '## participants | 32 nodes, common delivery, 2 erased, 1% loss, 5 ms jitter | Q2 3 wire=tak_lean align=1 nodes=32 fanout=common lossProb=0.01 jitterMaxUs=5000'; ./ar_sim Q2 3 wire=tak_lean align=1 nodes=32 fanout=common lossProb=0.01 jitterMaxUs=5000
echo '## participants | 16 nodes, common delivery, PHY 10000 kbit/s | Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=10000000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=10000000
echo '## participants | 16 nodes, common delivery, PHY 5000 kbit/s | Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=5000000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=5000000
echo '## participants | 16 nodes, common delivery, PHY 2000 kbit/s | Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=2000000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=2000000
echo '## participants | 16 nodes, common delivery, PHY 1000 kbit/s | Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=1000000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=16 fanout=common phyBps=1000000
echo '## participants | 16 nodes, common delivery, correlated 3% loss | R0 10 wire=tak_lean align=1 nodes=16 fanout=common lossProb=0.03 correlatedLoss=1'; ./ar_sim R0 10 wire=tak_lean align=1 nodes=16 fanout=common lossProb=0.03 correlatedLoss=1
echo '## routes | 4 nodes, unicast, 1 hop, 1 input/s, reserve 56 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=1 inputHz=1 lendUrgentShare=0 urgentShareBits=56000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=1 inputHz=1 lendUrgentShare=0 urgentShareBits=56000
echo '## routes | 4 nodes, unicast, 1 hop, 0.5 input/s, reserve 40 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=1 inputHz=0.5 lendUrgentShare=0 urgentShareBits=40000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=1 inputHz=0.5 lendUrgentShare=0 urgentShareBits=40000
echo '## routes | 4 nodes, unicast, 2 hop, 1 input/s, reserve 104 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=2 inputHz=1 lendUrgentShare=0 urgentShareBits=104000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=2 inputHz=1 lendUrgentShare=0 urgentShareBits=104000
echo '## routes | 4 nodes, unicast, 2 hop, 0.5 input/s, reserve 80 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=2 inputHz=0.5 lendUrgentShare=0 urgentShareBits=80000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=2 inputHz=0.5 lendUrgentShare=0 urgentShareBits=80000
echo '## routes | 4 nodes, unicast, 3 hop, 1 input/s, reserve 152 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=3 inputHz=1 lendUrgentShare=0 urgentShareBits=152000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=3 inputHz=1 lendUrgentShare=0 urgentShareBits=152000
echo '## routes | 4 nodes, unicast, 3 hop, 0.5 input/s, reserve 112 kbit | Q0 3 wire=tak_lean align=1 nodes=4 hops=3 inputHz=0.5 lendUrgentShare=0 urgentShareBits=112000'; ./ar_sim Q0 3 wire=tak_lean align=1 nodes=4 hops=3 inputHz=0.5 lendUrgentShare=0 urgentShareBits=112000
