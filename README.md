# http2 - mqtt

It's a demo test mqtt and http2.

## Download

```
git clone https://github.com/wanghaEMQ/kuma
git submodule update --init
```

## Requires

+ paho-mqtt([paho-mqtt-c](https://github.com/eclipse/paho.mqtt.c), [paho-mqtt-cpp](https://github.com/eclipse/paho.mqtt.cpp))
+ gcc, g++
+ openssl, openssl-dev(linux)

## Build

### build *libev* first

```
$ cd /path/to/the/kuma
$ cd ./third_party/libkev
(linux)
$ python bld/linux/build_linux.py
(mac)
$ python bld/mac/build_mac.py
```

### Build libkuma

```
$ cd /path/to/the/kuma
(linux)
$ python ./bld/linux/build_linux.py
(mac)
$ python ./bld/mac/build_mac.py
```

Since we don't want to install the libkuma, so we need to add the libkuma
to the path system can find.

```
$ cd /path/to/the/kuma

(linux)
$ export LD_LIBRARY_PATH=./bin/linux/:$LD_LIBRARY_PATH
(maybe)
$ export LD_LIBRARY_PATH=./bin/linux/Release/:$LD_LIBRARY_PATH
(mac)
$ export LD_LIBRARY_PATH=./bin/mac/:$LD_LIBRARY_PATH

sudo ldconfig
```

If the path of libkuma.so is bin/linux/Release, then add ` BINDIR = $(KUMADIR)/bin/linux/Release ` to the test/client/Makefile

### Build demo

```
$ cd /path/to/the/kuma
$ cd test/client
$ make
```

## Run demo

```
$ cd /path/to/the/kuma
(linux)
$ ./bin/linux/client
(maybe)
$ ./bin/linux/Release/client
(mac)
$ ./bin/mac/client
```

Input *c* would stop the client.

Input *g* would send a get request.

Input *p* would send a post request.

Input *r* would do a reconnect.

Input *test* would send a mqtt request.

## Note

1. Run export command in every shell session.

2. Update your g++ & gcc if failed in building.

3. **After openssl installed** if some error happend about openssl, Comment the CMakeLists.txt:77.

Please refer to README.md.origin for more information about kuma. 

