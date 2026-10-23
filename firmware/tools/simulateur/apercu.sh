#!/bin/sh
# Aperçu des écrans sur PC, sans flasher : compile le VRAI code de rendu
# (src/ui.cpp, src/pages.cpp, src/app.cpp) avec une imitation de l'écran
# et des réponses de passerelle d'exemple (fixtures/*.json).
# Prérequis : g++, Python 3 + Pillow, et ArduinoJson (téléchargé par PlatformIO).
set -e
cd "$(dirname "$0")"
ARDUINOJSON="${ARDUINOJSON:-../../.pio/libdeps/smalltv/ArduinoJson/src}"
g++ -std=gnu++17 -O1 -w -I shim -I ../../src -I "$ARDUINOJSON" \
    sim.cpp ../../src/ui.cpp ../../src/pages.cpp ../../src/app.cpp ../../src/config.cpp -o sim
mkdir -p out
./sim fixtures/
g++ -std=gnu++17 -O1 -w -I shim -I ../../src test_config.cpp ../../src/config.cpp -o test_config
./test_config
python3 planche.py
