# Serial Communication Library

This is a single header library for interfacing with rs-232 serial like ports written in C++.

## Quick Start

This library is very intuitive to use, here is an example of how it can be used to list all available serial ports.

```c++
#include <iostream>
#include "serial.h"

using namespace std;
using namespace serial;

int main(int argc, char* argv[]) {
    for (const auto& port : SerialInfo::ListPort()) {
        cout << port << endl;
    }
}
```

The next example shows how to send and receive serial data.

```c++
#include <iostream>
#include "serial.h"

using namespace std;
using namespace serial;

int main(int argc, char* argv[]) {
    Serial serial("ttyACM0", 115200);
    uint8_t buf[10];

    serial.Init();
    while (true) {
        serial.Read(buf, 10);
        serial.Write(buf, 10);
    }
}
```

Note that the read operation blocks until at least one byte data has been received.
