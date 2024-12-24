# multifan

`tl;dr` Sets fans' PWM values based on a number of inputs.

If you're currently using `fancontrol` from `lm_sensors` and you're happy with
it, there's really no reason to switch.

There's no real configuration in place right now, so if you want to use it,
you'll have to adapt `main()` to your own system/needs and recompile. The code
should make it possible to have multiple sources aggregated in a number of
different ways driving multiple fans, but my needs are very minimal : one fan
and two sources.

My main use case is that I want the single fan in my system to depend on the
temperature of both the CPU and the hard drive, while `fancontrol` only allows
one temperature input to drive a PWM value. Perhaps there are programs that
allow this but I haven't found any, on the other hand I haven't really looked
very hard.
