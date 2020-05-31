Prefetch exception handler from dwelch67/mmu prints
last value of R0 = 0000A1E0
DFSR= 000008A7 => [2:0] = 0b111 => translation fault page from manual.
IFSR= 00001629
lr = 00008700
instruction = E92D4010 => e92d4010 push {r4, lr}
00008040

dump of data abort exception
00022680 00000001 00001629 00010584 E1D400B7 00022220
Last Ro value, DFSR, IFSR, LR, instruction, i don't know this

| Last Ro value 	| DFSR     	| IFSR     	| LR       	| instruction 	| Unkwown  	|
|---------------	|----------	|----------	|----------	|-------------	|----------	|
| 00022680      	| 00000001 	| 00001629 	| 00010584 	| E1D400B7    	| 00022220 	|
