# Radio driver test seams and calibration excerpt

`calibration_config.cpp` is the exact `SX126x::config` method from RadioLib
7.7.1, `src/modules/SX126x/SX126x_config.cpp`, copyright 2018 Jan Gromeš.
The upstream MIT terms are retained in
[the RadioLib license](../../fixtures/radiolib_busy/license.txt).

The complete upstream source SHA-256 is
`eeff40cd4559038843aa235ec34385b5d02f75e2793d3f38c1a6bc7f05842ec3`;
the exact method excerpt SHA-256 is
`271f9b4a58f1e0b91560694fc6271c102efa0ab793bf16a723ef6bb6f9ad7337`.
The source generator admits these bytes before applying the bounded calibration
wait. Preserve the excerpt bytes and upstream notice when redistributing it.

The remaining headers provide controlled SDK/peripheral test seams. They do not
establish physical SPI, RF, GPIO or shutdown behavior.
