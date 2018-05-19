# lfrtm

## Minimalistic concurrent lock-free realtime-safe memory allocator

### Properties

* Is lock-free
* Is wait-free
* Is rt-safe
* Uses a simplistic API

### Build Status

[![build status](https://gitlab.com/OpenMusicKontrollers/lfrtm/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/lfrtm/commits/master)

### Build / test

	git clone https://git.open-music-kontrollers.ch/lv2/lfrtm
	cd lfrtm
	meson build
	cd build
	ninja -j4
	ninja test

### License

Copyright (c) 2018 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.
