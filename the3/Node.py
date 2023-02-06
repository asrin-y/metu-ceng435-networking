import socket
import sys
import threading
import time

# receiveThread is used to receive distance vectors from neighbours.
def receiveThread(connectionSock: socket.socket):
    global ownPort, receivedDistanceVector, timeoutFlag
    while not timeoutFlag:
        time.sleep(0.1)
        try:
            receive = connectionSock.recv(4096, socket.MSG_DONTWAIT)
            receivedMSG = receive.decode()
            Lines = receivedMSG.split("\n")
            Lines.pop()
            receivedDistanceVector = Lines
            computeDistanceVector()
        except BlockingIOError as e:
            pass

# sendThread is used to send distance vectors to neighnours when it is updated
def sendThread(connectionSock: socket.socket):
    global distanceVector, distanceVectorUpdatedFlag, ownPort, timeoutFlag
    while not timeoutFlag:
        if distanceVectorUpdatedFlag:
            time.sleep(1)
            sendItem = "".join(distanceVector)
            connectionSock.send(sendItem.encode())
            distanceVectorUpdatedFlag = False

# timerThread is used to calculate time in order to terminate the
# program when all distance vectors set. In this implementation,
# timeout flag is set when most recent update is 5 seconds ago.
def timerThread():
    global currentTime, timeoutFlag
    while not timeoutFlag:
        time.sleep(0.1)
        currentTime = time.time()
        if currentTime - prevTime > 5:
            timeoutFlag = True


def resetTimer():
    global prevTime
    prevTime = time.time()

# distanceTo function gives the distance to given destination
# using the distance vectors of current node
def distanceTo(destination):
    global distanceVector
    for line in distanceVector:
        words = line.split()
        if int(words[1]) == destination or int(words[0]) == destination:
            return int(words[2])
    return 8223372036854775807

# updateDistanceVector updates the destination to distance in
# node's own distance vector using received distance vector.
def updateDistanceVector(source, destination, distance):
    global distanceVector
    resetTimer()
    i = -1
    for line in distanceVector:
        i += 1
        words = line.split()
        if (int(words[0]) == source and int(words[1]) == destination) or \
                (int(words[0]) == destination and int(words[1]) == source):
            words[2] = distance
            distanceVector[i] = f"{words[0]} {words[1]} {str(words[2])}\n"
            return
    newLine = f"{source} {destination} {distance}\n"
    distanceVector.append(newLine)

# computeDistanceVector function checks if the recieved distance
# vector has shorter path than current node's own distance vector.
# If there is a shorter path, this function calls updateDistanceVector
# function and sets global "updated" flag to "True" to notify sendThread.
def computeDistanceVector():
    global receivedDistanceVector, distanceVector, distanceVectorUpdatedFlag, ownPort
    updated = False
    for line in receivedDistanceVector:
        words = line.split()
        receivedSource = int(words[0])
        receivedDestination = int(words[1])
        receivedDistance = int(words[2])
        neighbourThenDestination = distanceTo(receivedSource) + receivedDistance
        directDestination = distanceTo(receivedDestination)
        if neighbourThenDestination < directDestination:
            updateDistanceVector(ownPort, receivedDestination, neighbourThenDestination)
            updated = True
    if updated:
        distanceVectorUpdatedFlag = True





# Check command line arguments
if len(sys.argv) != 2:
    print("usage: ./python3 server.py <port>")
    sys.exit()


# Create the node's own socket
ownSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
host = "127.0.0.1"
ownPort = int(sys.argv[1])
try:
    ownSocket.bind((host, ownPort))
except:
    print("bind error")
    sys.exit()


# Wait for others to create their sockets
time.sleep(5)


# Read the node's immediate neighbours' distances
file1 = open(str(ownPort) + ".costs", 'r')
distanceVector = file1.readlines()
node_count = int(distanceVector.pop(0))
file1.close()


# Listen immediate neighbours' connection requests
ownSocket.listen(node_count)


sendtoNeighboursSockets = []
receiveFromNeighboursSockets = []
receiveThreads = []
sendThreads = []
receivedDistanceVector = []


# Create sockets and threads for every immediate neighbours and connect to them
i = -1
for line in distanceVector:
    i += 1
    time.sleep(0.1)
    words = line.split()
    sendtoNeighboursSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    host = "127.0.0.1"
    port = int(words[0])
    try:
        sendtoNeighboursSocket.connect((host, port))
    except:
        print(f"{ownPort}: connection to {port} is refused")
    sendtoNeighboursSockets.append(sendtoNeighboursSocket)
    threadSend = threading.Thread(target=sendThread, args=(sendtoNeighboursSocket,), name=f"sendThread{port}")
    sendThreads.append(threadSend)
    distanceVector[i] = str(ownPort) + " " + distanceVector[i]

for line in distanceVector:
    receiveFromNeighboursSocket, address = ownSocket.accept()
    receiveFromNeighboursSockets.append(receiveFromNeighboursSocket)
    threadReceive = threading.Thread(target=receiveThread, args=(receiveFromNeighboursSocket,),
                                    name=f"receiveThread{port}")
    receiveThreads.append(threadReceive)

distanceVector.insert(0, f"{ownPort} {ownPort} 0\n")
distanceVectorUpdatedFlag = True

timeoutFlag = False

for threads in sendThreads:
    threads.start()

for threads in receiveThreads:
    threads.start()

currentTime = 0
prevTime = time.time()
threadTime = threading.Thread(target=timerThread)
threadTime.start()

while not timeoutFlag:
    time.sleep(1)

printValue = "\n"
for line in distanceVector:
    words = line.split()
    printValue += f"{words[0]} - {words[1]} | {words[2]}\n"
printValue += "\n\n"
print(printValue)
ownSocket.close()