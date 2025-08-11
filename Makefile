CXX = g++
CXXFLAGS = -Wall

# Opções do linker
LDFLAGS = -L$(HOME)/MQTTlibs/paho.mqtt.c/src/ -L$(HOME)/MQTTlibs/paho.mqtt.cpp/src/
# Adiciona as bibliotecas SQLite3 e cURL
LDLIBS = -lpaho-mqttpp3 -lpaho-mqtt3as -lpaho-mqtt3c -lssl -lcrypto -lpthread -lsqlite3 -lcurl

SRC = ./Controladora.cpp
OUT = ./main

all:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) -o $(OUT)