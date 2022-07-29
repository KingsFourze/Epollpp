# Epollpp - A Simple Epoll TCP Server Library.

## Supported Platform

* Linux (Kernel 2.6.17 or newer), but only tested on AlmaLinux 8.6 x86_64 (Kernel 4.18.0-372)

## Installation

1. Download this repo
1. Extract and move to `/usr/local/include` or any path you like
1. Done.

## Notice

This Library is using pthread.h for multi-threading. So you must link the pthread library using `-pthread` argument when compile.

Eample for gcc: `g++ main.cpp -o main.exe -pthread`

## Usage

Example Echo Server

```
#include <iostream>
#include <cstring>
#include <vector>
#include <unistd.h> // for usleep

// You need to put it in /usr/local/include
// Otherwize, include it in a right way.
#include <Epollpp/Epollpp.hpp>

using namespace std;
using namespace Epollpp;

int main(){
    // Set the port on 9090, ETMode set to false because not support now.
    // Write a lambda function for processing data-in.
    // Write another function for cleanup or Write a empty function.
    EpollServer server(9090, false,
        [](EpollServer* server, TCPConn* client)->void{
            vector<string>* msgs = client->Recv("\r\n");
            if (msg == nullptr) return;

            for (int i = 0; i < msg->size(); i++){
                cout << msg->at(i) << endl; // Print Message
                client->Send(msg->at(i)); // Send Message back to client.
            }

            delete msgs;
        },
        [](TCPConn* client)->void{
            cout << "Client Exited. FD:" << client->originFD << endl;
        }
    );
    
    // Start Server
    server.Start();
    
    while (true){
        usleep(1000);
    }
    return 0;
}
```

## TODO
- [x] A Basic Epoll Server
- [ ] Support ET Mode
