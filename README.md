# http2 - mqtt converter

It's a demo for converting mqtt to http2.

## Requires

+ paho-mqtt

## Build

### Build libkuma

```
$ cd /path/to/the/kuma
$ cd src
$ make
```

Since we don't want to install the libkuma, so we need to add the libkuma
to the path system can find.

```
$ cd /path/to/the/kuma
$ export LD_LIBRARY_PATH=./bin/linux/:$LD_LIBRARY_PATH
```

### Build demo

```
$ cd /path/to/the/kuma
$ cd test/client
$ make
```

## Run demo

```
$ cd /path/to/the/kuma
$ ./bin/linux/client
```

Input *c* would stop the client.
Input *g* would send a get request.
Input *p* would send a post request.
Input *r* would do a reconnect.
Input *test* would send a mqtt request.

## Note

1. Maybe you should build *libev* independently.

```
$ cd ./third_party/libkev
$ python bld/linux/build_linux.py
```

2. Run export command in every shell session.

Please refer to README.md.origin for more information about kuma. 

