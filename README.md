# Epollpp - A Simple Epoll TCP Server Library.

## Installation

1. Download this repo
1. Extract and move to `/usr/local/include` or any path you like
1. Done.

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
    EpollServer server(9090, false, [](EpollServer* server, TCPConn* client)->void{
        vector<string>* msgs = client->Recv("\r\n");
        if (msg == nullptr) return;

        for (int i = 0; i < msg->size(); i++){
            cout << msg->at(i) << endl; // Print Message
            client->Send(msg->at(i)); // Send Message back to client.
        }

        delete msgs;
    });
    
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
