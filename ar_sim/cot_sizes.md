# Measured CoT sizes (largest of five instances; charged = payload + 28 B UDP/IPv4 + 28 B authentication)

## as_specified

| message | profile abstract | xml | xml + deflate | xml + dictionary deflate | TAK v1 (detail as text) | binary AR detail (new TAK version) |
|---|---|---|---|---|---|---|
| urgent | 800 | 1219 / 1275 | 628 / 684 | 178 / 234 | 1007 / 1063 | 328 / 384 |
| state | 800 | 935 / 991 | 535 / 591 | 114 / 170 | 723 / 779 | 276 / 332 |
| control | 400 | 664 / 720 | 385 / 441 | 66 / 122 | 431 / 487 | 171 / 227 |
| repair_request | 400 | 526 / 582 | 286 / 342 | 33 / 89 | 293 / 349 | 149 / 205 |
| repair_data | 200 | 1219 / 1275 | 629 / 685 | 180 / 236 | 1007 / 1063 | 328 / 384 |

Each cell: payload bytes / charged bytes.

## lean

| message | profile abstract | xml | xml + deflate | xml + dictionary deflate | TAK v1 (detail as text) | binary AR detail (new TAK version) |
|---|---|---|---|---|---|---|
| urgent | 800 | 656 / 712 | 381 / 437 | 135 / 191 | 500 / 556 | 245 / 301 |
| state | 800 | 550 / 606 | 340 / 396 | 116 / 172 | 394 / 450 | 215 / 271 |
| control | 400 | 384 / 440 | 224 / 280 | 35 / 91 | 206 / 262 | 122 / 178 |
| repair_request | 400 | 338 / 394 | 189 / 245 | 30 / 86 | 159 / 215 | 108 / 164 |
| repair_data | 200 | 656 / 712 | 381 / 437 | 139 / 195 | 500 / 556 | 245 / 301 |

Each cell: payload bytes / charged bytes.

TAK v1 round trip (envelope values and detail text identical after decode): pass
