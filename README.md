## Raspberry Pi 4 Platform

The following are needed to run platform functionality:

```
sudp apt install libmodbus-dev
```

CMakeLists comes pre-configured. Just be sure to have CMake installed.

```
sudo apt install cmake
```

If any of the depedencies are not found, run this to update repository:

```
sudo apt update
```

## Run Tests

```
mkdir -p ./build
cd ./build
cmake ..
make
./tests/tests
```
