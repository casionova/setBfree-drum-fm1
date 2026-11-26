# CB-931 drums (reference)

`drums.c` and `drums.h` are the Cheesebox 931's rhythm-section engine, copied unchanged from CB-931 FM-1 0.16.1
(`src/drums.c`, `src/drums.h`, by Skipper). They are float code and never go into the firmware. Two things use them:

- `tools/gen_drums.c` reads the kits from them and writes `firmware/src/drum_tab.h`, the integer tables the FM-1's
  engine (`firmware/src/drum_core.h`) plays from;
- `tests/drum_test.c` plays every sound through both engines and compares them.

To take new kit values from CB-931: copy the two files over these, run `tools/gen_drums.sh`, run the test.
