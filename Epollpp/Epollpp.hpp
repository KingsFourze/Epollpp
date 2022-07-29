#ifndef __EPOLLPP_HPP__
#define __EPOLLPP_HPP__

#include <iostream>
#include <functional>
#include <pthread.h>
#include <cstring>
#include <sstream>
#include <vector>
#include <unordered_map>

#include <fcntl.h> // non-blocking
#include <sys/socket.h> // socket
#include <netinet/in.h> // socket_addr
#include <sys/epoll.h> // epoll
#include <unistd.h> // close, usleep

#define MAXCLIENTS 1000
#define MAXEVENTSIZE 32

using namespace std;

namespace Epollpp{

    class StringHandler{
    public:
        static vector<string>* Split(const string& source, const string& delimiter){
            vector<string>* strParts = new vector<string>();

            string unSplited = source;
            while (unSplited.length() != 0){
                int index = unSplited.find(delimiter);
                strParts->push_back(unSplited.substr(0, index));

                if (index == -1) break;

                unSplited = unSplited.substr(index + delimiter.length());
            }

            return strParts;
        }
        static vector<string>* Split(const char* source, const string& delimiter){
            return Split(string(source), delimiter);
        }
        
        static bool StartWith(const string& source, const string& start){
            return source.substr(0, start.length()) == start;
        }
        static bool StartWith(const char* source , const string& start){
            return StartWith(string(source), start);
        }
        
        static bool EndWith(const string& source, const string& end){
            return source.substr(source.length() - end.length() , -1) == end;
        }
        static bool EndWith(const char* source, const string& end){
            return EndWith(string(source), end);
        }
    };

    class TCPConn{
    private:
        pthread_mutex_t lock_send = PTHREAD_MUTEX_INITIALIZER;
        int clientFD = -1;
        std::stringstream ssBuffer;
    public:
        int originFD = -1;
        TCPConn(int clientFD);
        char* Recv(int);
        std::vector<std::string>* Recv(std::string delimiter);
        void Send(std::string&);
        void Send(const char*, int);
        void Close();
    };

    TCPConn::TCPConn(int clientFD){
        this->clientFD = clientFD;
        this->originFD = clientFD;
        this->ssBuffer.str(std::string());
    }

    char* TCPConn::Recv(int size){
        if (clientFD == -1) return nullptr;

        char* buffer = new char[size]();
        memset(buffer, '\0', size);

        int recvLen = 0;
        int tryCount = 0;

        int result = 0;
        while (true){
            result = recv(clientFD, buffer + recvLen, size - recvLen, MSG_DONTWAIT);
            if (result > 0){
                recvLen += result;
                if (recvLen == size)
                    break;
            }
            else if (result < 0 && errno == EAGAIN){
                if (++tryCount == 5){
                    this->Close();
                    return nullptr;
                }
                usleep(1000);
            }
            else{
                this->Close();
                return nullptr;
            }
        }
        return buffer;
    }

    std::vector<std::string>* TCPConn::Recv(std::string delimiter){
        if (clientFD == -1) return nullptr;

        char buffer[1024];

        std::vector<std::string>* msgs = nullptr;
        int result = 0;
        int tryCount = 0;
        while (true){
            memset(buffer, '\0', 1024);
            result = recv(clientFD, buffer, 1024, MSG_DONTWAIT);
            if (result > 0){
                ssBuffer << buffer;

                if (ssBuffer.str().length() < delimiter.length()) continue;
                if (StringHandler::EndWith(ssBuffer.str(), delimiter)){
                    msgs = StringHandler::Split(ssBuffer.str(), delimiter);
                    ssBuffer.str(std::string());
                    break;
                }
            }
            else if (result == -1 && errno == EAGAIN){
                if (++tryCount == 3){
                    msgs = StringHandler::Split(ssBuffer.str(), delimiter);
                    ssBuffer.str(std::string());
                    ssBuffer << msgs->at(msgs->size() -1);
                    msgs->pop_back();
                    break;
                }
                usleep(1000);
            }
            else{
                this->Close();
                break;
            }
        }

        return msgs;
    }

    void TCPConn::Send(std::string& msg){
        Send(msg.c_str(), msg.length());
    }

    void TCPConn::Send(const char* msg, int length){
        if (length == 0) return;
        if (clientFD == -1) return;
        int tryCount = 0;
        int sentCount = 0;
        int result = 0;
        pthread_mutex_lock(&lock_send);
        while (true){
            result = send(clientFD, msg + sentCount, length - sentCount, MSG_DONTWAIT);
            if (result > 0){
                sentCount += result;
            }
            else if (result < 0 && errno == EAGAIN){
                if (++tryCount == 3 && sentCount == 0) break; // drop the packet when can't send.
                usleep(1000);
            }
            else{
                break;
            }
        }
        pthread_mutex_unlock(&lock_send);
    }

    void TCPConn::Close(){
        if (clientFD == -1) return;
        close(clientFD);
        clientFD = -1;
    }

    class EpollServer{
    private:
        pthread_mutex_t lock_clients = PTHREAD_MUTEX_INITIALIZER;
        pthread_t mainThread, heartbeatThread;

        int port;
        bool ETmode = false;
        int socketFD = -1, epollFD = -1;
        sockaddr_in sockAddr;
        std::function<void(EpollServer*, TCPConn*)> ProcessFunc;
        std::function<void(TCPConn*)> CleanUpFunc;
        void Accept();
    public:
        std::unordered_map<int, TCPConn*> fd2Clients;

        EpollServer(int, bool, std::function<void(EpollServer*, TCPConn*)>, std::function<void(TCPConn*)>);
        void Start();
        void Remove(int);
        void Process(int);
    };

    EpollServer::EpollServer(int port, bool ETmode, std::function<void(EpollServer*, TCPConn*)> ProcessFunc, std::function<void(TCPConn*)> CleanUpFunc){
        if (port < 1 || port > 65535){
            std::cout << "Port: " << port << " is not Avaliable." << std::endl;
            exit(1);
        }
        this->port = port;
        this->ProcessFunc = ProcessFunc;
        this->CleanUpFunc = CleanUpFunc;
    }

    void EpollServer::Start(){
        sockAddr.sin_port = htons(this->port);
        sockAddr.sin_family = AF_INET;
        sockAddr.sin_addr.s_addr = htons(INADDR_ANY);

        socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bind(socketFD, (sockaddr *) &sockAddr, sizeof(sockAddr)) == -1){
            std::cout << "Socket Bind Failure." << std::endl;
            exit(1);
        }
        if (listen(socketFD, 16) == -1){
            std::cout << "Socket Listen Failure." << std::endl;
            exit(1);
        }

        epollFD = epoll_create(1);
        epoll_event epollEvent{};
        epollEvent.events = EPOLLIN;
        epollEvent.data.fd = socketFD;
        epoll_ctl(epollFD, EPOLL_CTL_ADD, socketFD, &epollEvent);
        pthread_create(&mainThread, nullptr,
            [](void* ptr)->void*{
                EpollServer* epollServer = (EpollServer*) ptr;
                epoll_event events[MAXEVENTSIZE];

                TCPConn* clientConn = nullptr;

                int eventCount = 0;
                while (true){
                    eventCount = epoll_wait(epollServer->epollFD, events, MAXEVENTSIZE, -1);
                    if (eventCount == -1){
                        std::cout << "Epoll Error!" << std::endl;
                        exit(1);
                    }
                    
                    if (eventCount == 0){
                        usleep(10);
                        continue;
                    }

                    for (int i = 0; i < eventCount; i++){
                        if (events[i].data.fd == epollServer->socketFD){ // Accept Client.
                            epollServer->Accept();
                        }
                        else if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP || events[i].events & EPOLLRDHUP){ // When Error or Disconnect.
                            epollServer->Remove(events[i].data.fd);
                        }
                        else if (events[i].events & EPOLLIN){ // Data coming.
                            epollServer->Process(events[i].data.fd);
                        }
                    }
                }
                return nullptr;
            }
        , this);
    }

    void EpollServer::Accept(){
        sockaddr_in clientAddr{};
        socklen_t length = sizeof(clientAddr);
        int clientFD = accept(socketFD, (sockaddr *) &clientAddr, &length);
        TCPConn* clientConn = new TCPConn(clientFD);

        // get a free space to save connection
        pthread_mutex_lock(&lock_clients);
        if (fd2Clients.size() == MAXCLIENTS){ // if don't have enough space, close connection.
            pthread_mutex_unlock(&lock_clients);
            std::cout << "Not enough space to keep connection, close FD: " << clientFD << std::endl;
            clientConn->Close();
            delete clientConn;
            return;
        }

        int flags = fcntl(clientFD, F_GETFL, 0);
        if (flags < 0){
            pthread_mutex_unlock(&lock_clients);
            std::cout << "Can't set client to Non-blocking mode." << std::endl;
            clientConn->Close();
            return;
        }
        if (fcntl(clientFD, F_SETFL, flags | O_NONBLOCK) < 0){
            pthread_mutex_unlock(&lock_clients);
            std::cout << "Can't set client to Non-blocking mode." << std::endl;
            clientConn->Close();
            return;
        }

        fd2Clients[clientFD] = clientConn;

        epoll_event ClientInEvent{};
        ClientInEvent.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP; // Add "| EPOLLET" to activate ET mode.
        ClientInEvent.data.fd = clientFD;

        epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &ClientInEvent);
        pthread_mutex_unlock(&lock_clients);
    }

    void EpollServer::Remove(int clientFD){
        pthread_mutex_lock(&lock_clients);
        TCPConn* clientConn = nullptr;

        std::unordered_map<int, TCPConn*>::iterator itConn = fd2Clients.find(clientFD);
        if (itConn == fd2Clients.end()){
            pthread_mutex_unlock(&lock_clients);
            return;
        }
        clientConn = itConn->second;
        fd2Clients.erase(itConn);
        pthread_mutex_unlock(&lock_clients);

        if (clientConn != nullptr){
            epoll_ctl(epollFD, EPOLL_CTL_DEL, clientConn->originFD, nullptr);

            CleanUpFunc(clientConn);
            clientConn->Close();
            delete(clientConn);
        }
    }

    void EpollServer::Process(int clientFD){
        pthread_mutex_lock(&lock_clients);
        TCPConn* clientConn = nullptr;

        std::unordered_map<int, TCPConn*>::iterator itConn = fd2Clients.find(clientFD);
        if (itConn == fd2Clients.end()){
            pthread_mutex_unlock(&lock_clients);
            return;
        }
        clientConn = itConn->second;
        pthread_mutex_unlock(&lock_clients);

        this->ProcessFunc(this, clientConn);
    }

}
#endif