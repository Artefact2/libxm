libxm
=====

A small XM (FastTracker II Extended Module) player library. Designed
for easy integration in demos and such, and provides timing functions
for easy sync (well, not yet, but it's planned).

Written in C11 and released under the WTFPL license, version 2.

Examples
========

Two example programs are provided.

* `xmtoau` will play a module and output a `.au` file to standard
  output. Example usages:

  * Directly play a module (requires the `sox` package):

    ~~~
	./xmtoau my_module.xm | play -
	~~~

  * Convert the data to a .wav file on the fly, then play it with `mplayer`:

    ~~~
    ./xmtoau my_module.xm | ffmpeg -i - file.wav
    mplayer file.wav
	~~~

* `xmtoalsa` is a simple player that uses the ALSA library.

* `xmbench` is a benchmark program.

Status
======

Effects
-------

~~~
 Status |##| Eff | Info | Description
--------+--+-----+------+------------------------------
DONE    |00|  0  |      | Arpeggio
        |01|  1  |  (*) | Porta up
        |02|  2  |  (*) | Porta down
        |03|  3  |  (*) | Tone porta
        |04|  4  |  (*) | Vibrato
PARTIAL |05|  5  |  (*) | Tone porta+Volume slide
PARTIAL |06|  6  |  (*) | Vibrato+Volume slide
        |07|  7  |  (*) | Tremolo
        |08|  8  |      | Set panning
        |09|  9  |      | Sample offset
DONE    |10|  A  |  (*) | Volume slide
DONE    |11|  B  |      | Position jump
DONE    |12|  C  |      | Set volume
DONE    |13|  D  |      | Pattern break
        |14|  E1 |  (*) | Fine porta up
        |--|  E2 |  (*) | Fine porta down
        |--|  E3 |      | Set gliss control
        |--|  E4 |      | Set vibrato control
        |--|  E5 |      | Set finetune
        |--|  E6 |      | Set loop begin/loop
        |--|  E7 |      | Set tremolo control
        |--|  E9 |      | Retrig note
DONE    |--|  EA |  (*) | Fine volume slide up
DONE    |--|  EB |  (*) | Fine volume slide down
        |--|  EC |      | Note cut
        |--|  ED |      | Note delay
        |--|  EE |      | Pattern delay
DONE    |15|  F  |      | Set tempo/BPM
DONE    |16|  G  |      | Set global volume
DONE    |17|  H  |  (*) | Global volume slide
        |19|
DONE    |20|  K  |      | Key off              (Also note number 97)
        |21|  L  |      | Set envelope position
        |22|
        |23|
        |24|  P  |  (*) | Panning slide
        |25|
        |26|  R  |  (*) | Multi retrig note
        |27|
        |28|  T  |      | Tremor
        |31|  X1 |  (*) | Extra fine porta up
        |--|  X2 |  (*) | Extra fine porta down
~~~

Volume effects
--------------

~~~
 Status |  Value  | Meaning
--------+---------+-----------------------------
DONE    | $10-$50 | Set volume (Value-$10)
DONE    | $60-$6f | Volume slide down
DONE    | $70-$7f | Volume slide up
DONE    | $80-$8f | Fine volume slide down
DONE    | $90-$9f | Fine volume slide up
        | $a0-$af | Set vibrato speed
        | $b0-$bf | Vibrato
        | $c0-$cf | Set panning
        | $d0-$df | Panning slide left
        | $e0-$ef | Panning slide right
        | $f0-$ff | Tone porta
~~~

General
-------

* Envelopes: volume OK, panning TODO
* Autovibrato: TODO
* Frequence tables: linear OK, amiga TODO
* Ping-pong loop: TODO

Thanks
======

Thanks to:

* Thunder <kurttt@sfu.ca>, for writing the `modfil10.txt` file;

* Matti "ccr" Hamalainen <ccr@tnsp.org>, for writing the `xm-form.txt`
  file;

* Mr.H of Triton and Guru and Alfred of Sahara Surfers, for writing
  the specification of XM 1.04 files.
