libxm
=====

A small XM (FastTracker II Extended Module) player library. Designed
for easy integration in demos and such, and provides timing functions
for easy sync (well, not yet, but it's planned).

Written in C11 and released under the WTFPL license, version 2.

Examples
========

Two example programs are provided.

`xmtest` will play a module but will not produce any sound or
output. Useful for testing the playback routines.

`xmtoalsa` is a simple player that uses the ALSA library.

Thanks
======

Thanks to:

* Thunder <kurttt@sfu.ca>, for writing the `modfil10.txt` file;

* Matti "ccr" Hamalainen <ccr@tnsp.org>, for writing the `xm-form.txt`
  file;

* Mr.H of Triton and Guru and Alfred of Sahara Surfers, for writing
  the specification of XM 1.04 files.
