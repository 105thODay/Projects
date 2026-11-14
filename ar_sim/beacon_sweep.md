# Power save and the beacon interval (Rev 6)

8 participants, common delivery, lean CoT XML, alignment on, human renewal input, no injected loss (Q0), 802.11 AP
channel, DTIM period 1, one station in power save (`wifiDozing=1`), three seeds of 30 s. Command:
`ar_sim Q0 3 nodes=8 fanout=common sharedEncode=1 lendUrgentShare=0 repairShareBits=8000 controlHz=1 repairHz=0
stateHz=10 durationS=30 wire=xml_lean align=1 urgentShareBits=121632 channel=wifi_ap inputMinGapUs=154000
wifiDozing=1 group=<basic|unicast> wifiBeaconUs=<interval>`

| relay | beacon interval | late of 1,351 | L P50 / P95 / max ms | channel busy |
|---|---|---|---|---|
| group frames at basic rate | 102.4 ms (100 TU) | 826 | 113 / 146 / 164 | 12.1 % |
| group frames at basic rate | 51.2 ms | 49 | 78 / 99 / 106 | 12.4 % |
| group frames at basic rate | 20.48 ms | 0 | 60 / 71 / 74 | 13.7 % |
| group frames at basic rate | 10.24 ms | 0 | 55 / 62 / 66 | 15.4 % |
| converted to unicast | 102.4 ms | 100 | 50 / 123 / 155 | 15.3 % |
| converted to unicast | 51.2 ms | 3 | 50 / 80 / 104 | 15.8 % |
| converted to unicast | 20.48 ms | 0 | 50 / 61 / 74 | 16.9 % |
| converted to unicast | 10.24 ms | 0 | 50 / 55 / 63 | 18.7 % |

With one phone dozing, the AP holds every group frame until after the next DTIM beacon, and every frame for that
phone until its next beacon. At the common 100 TU interval that hold alone can exceed the 100 ms limit.
