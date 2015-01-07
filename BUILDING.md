Building libxm
==============
libxm uses cmake to build and compile, handle options.

Building with standard settings
-------------------------------
To make libxm with all standard settings, simply go to the root directory and run ``make``. This will setup the cmake build files in the ``build`` folder. Once you change to that folder, type ``make`` again and your files will be built.

```
$ make
...

$ cd build/

$ make
Scanning dependencies of target xm
...
[ 40%] Built target xm
...
[ 80%] Built target xms
...
[100%] Built target xmtoau

$ ls examples/
CMakeFiles     Makefile     cmake_install.cmake     xmbench     xmtoau
```

You can then use the standard example programs ``xmbench`` and ``xmtoau``, or the library build in the ``build/src`` folder.

Building with custom settings
-----------------------------
To make libxm with custom settings, we need to run cmake outselves, with our custom settings. We need to make and change to the ``build/`` folder, and then we can go ahead.

Here is an example of building with the ``XM_LINEAR_INTERPOLATION`` option disabled

```
$ mkdir -p build/

$ cd build/

$ cmake -DXM_LINEAR_INTERPOLATION=OFF ..
...
-- Generating done
-- Build files have been written to: /path/libxm/build

$ make
```

**Options**

* ``XM_LINEAR_INTERPOLATION``: Interpolate sound output (smooth out the sounds). This can make modules sound nicer and cleaner, but some modules sound better with this disabled (Default: ON).
* ``XM_RAMPING``: Use smooth volume/panning transitions (Default: ON).

* ``XM_BUILD_SHARED_LIBS``: Build shared library (Defualt: ON).
* ``XM_BUILD_EXAMPLES``: Build example programs (Defualt: ON).

* ``XM_DEBUG``: Debug messages (Default: ON).
* ``XM_BIG_ENDIAN``: Assume big endian byte order (default: OFF, little endian assumed).

As shown above, if you wish to enable an option, you would use the cmake argument ``-DOPTION_NAME=ON``, and if you wish to disable it you would use the argument ``-DOPTION_NAME=OFF``
