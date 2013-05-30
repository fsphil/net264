net264
======

Quick and dirty multi-client TCP streamer for the Raspberry Pi camera module

Build & Install:

make
sudo make install

Example Usage:

On the Raspberry Pi:
$ raspivid -t 1000000 -fps 25 -o - | net264

On a client PC:
$ nc <rpi ip address> 5500 | mplayer -demuxer h264es -fps 25 -

Or a client Raspberry Pi:
$ nc <rpi ip address> 5500 | omxplayer -

