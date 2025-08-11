CXX = g++
CXXFLAGS = -Wall

# Linker settings - Added -lmysqlclient for MySQL C API
LDFLAGS = -L$(HOME)/MQTTlibs/paho.mqtt.c/src/ -L$(HOME)/MQTTlibs/paho.mqtt.cpp/src/
LDLIBS = -lpaho-mqttpp3 -lpaho-mqtt3as -lpaho-mqtt3c -lssl -lcrypto -lpthread

SRC = ./Controladora.cpp
OUT = ./main

all:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) -o $(OUT)