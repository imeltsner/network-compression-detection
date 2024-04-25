# Network Compression Detection
This repo contains two applications that detect the presence of a compression link in a network. The first application operates using a client/server relationship,
while the second application is standalone. 

## Client/Server Application
### Overview
The client and server establish a TCP connection in order for the client to share a config file with the server. After parsing the config file,
the client sends two UDP packet trains to the server. The first packet train consists of low entropy data and the second packet train consists of high
entropy data. The server records the time difference between the first and last packet in each train. It then calculates the time difference between
the low entropy train and the high entropy train. The client then resestablishes a TCP connection with the server, and the server reports back to the client 
whether or not compression was detected. If the difference in arrival time between packet trains was greater than 100 milliseconds, the client will report 
"Compression Detected". If the difference was less than 100 milliseconds, the client will report "No compression detected".

### Usage
This application has two main files, server.c and client.c. They are meant to be run in two different terminals or virtual machines. 

1. Fill out the config.json file with the values you would like to use
2. In the first terminal, compile the server.c program
```
gcc -o server server.c config.c cJSON.c 
```
3. Start the server application
```
./server <port number>
```
4. In the second VM, compile the client.c program
```
gcc -o client client.c config.c cJSON.c
```
5. Start the client application
```
./client <path to config file>
```

The application will take some time to run, depending on how long you set the inter-measurement time. Once it completes,
you can observe the compression message in the terminal the client is running on. 