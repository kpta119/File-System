#!/bin/bash

if [ -z "$1" ]; then
  echo "Użycie: $0 <rozmiar_dysku_MB>"
  exit 1
fi

DISK_SIZE_MB=$1
DISK_FILENAME="vd.bin"
SHOW_HIDDEN=0

echo "To jest plik testowy 1:
taki plik 
plik raz dwa trzy
cztery 3222 21 1" > test_file_1.txt

echo "Inicjalizowanie dysku wirtualnego..."
./program 1 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 6
sleep 4

echo "--------------------------------------"
echo "--------------------------------------"
echo "TEST KOPIOWANIE PLIKU NA DYSK I Z POWROTEM ABY SPRAWDZIC CZY TEN SAM PLIK ZOSTANIE ODTWORZONY"
echo "Dodanie pliku test_file_1.txt na dysk"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 test_file_1.txt
sleep 5
echo "Wyswietlanie zawartosci pliku test_file_1.txt"
cat test_file_1.txt
sleep 5


echo "--------------------------------------"
echo "--------------------------------------"
echo "Usuwanie pliku test_file_1.txt z komputera"
rm -f test_file_1.txt
echo "Wyswietlenie plikow na komputerze po usuneiciu"
ls
sleep 5


echo "--------------------------------------"
echo "--------------------------------------"
echo "Kopiowanie pliku test_file_1.txt z dysku po usunieciu"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 2 test_file_1.txt
sleep 5


echo "--------------------------------------"
echo "--------------------------------------"
echo "Wyswietlanie zawartosci pliku test_file_1.txt po przywroceniu i ls"
ls
cat test_file_1.txt
sleep 5


echo "To jest plik testowy 2" > test_file_2.txt
echo "To jest plik testowy 3" > test_file_3.txt
echo "To jest plik testowy 4" > test_file_4.txt
echo "To jest plik ze spacjami w nazwie" > "dluga nazwa pliku ze spacjami.txt"
echo "Tresc pliku urytego" > .ukryty_plik.txt
echo "--------------------------------------"
echo "--------------------------------------"
echo "Test stworzenie i kopiowanie malych plikow na dysk"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 test_file_2.txt
sleep 1
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 test_file_3.txt
sleep 1
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 test_file_4.txt
sleep 1
echo "Kopiowanie na dysk pliku: dluga nazwa ze spacjami.txt"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 "dluga nazwa pliku ze spacjami.txt"
sleep 5

echo "--------------------------------------"
echo "--------------------------------------"
echo "Test listowania plikow na dysku"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 4
sleep 5

echo "--------------------------------------"
echo "--------------------------------------"
echo "Test wyswietlania bitmapy blokow"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 3
sleep 5

echo "--------------------------------------"
echo "--------------------------------------"
echo "Test dodanie kropkowego pliku i wylistowanie plikow bez wyswietlania ukrytych plikow"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 .ukryty_plik.txt
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 4
sleep 5

echo "--------------------------------------"
echo "--------------------------------------"
echo "Teraz wylistowanie plikow na dysku wraz z plikami kropkowymi"
SHOW_HIDDEN=1
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 4
sleep 5

echo "--------------------------------------"
echo "--------------------------------------"
echo "Usuwanie plikow drugiego i czwartego z dysku"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 5 test_file_2.txt
sleep 1
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 5 test_file_4.txt
sleep 4

echo "--------------------------------------"
echo "--------------------------------------"
echo "Listowanie plikow na dysku po usunieciu plikow"
SHOW_HIDDEN=1
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 4
sleep 3

echo "Wyswietlenie bitmapy blokow zajetych"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 3
sleep 4

echo "--------------------------------------"
echo "--------------------------------------"
echo "DODANIE DUZEGO PLIKU ABY POKAZAC NIECIGLA ALOKACJE"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 1 duzy_plik.txt

echo "--------------------------------------"
echo "--------------------------------------"
echo "Listowanie plikow na dysku"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 4
sleep 3

echo "--------------------------------------"
echo "--------------------------------------"
echo "Wyswietlania bitmapy blokow"
./program 0 $DISK_SIZE_MB $DISK_FILENAME $SHOW_HIDDEN 3
sleep 2


rm -f test_file_1.txt
rm -f test_file_2.txt
rm -f test_file_3.txt
rm -f test_file_4.txt
rm -f "dluga nazwa pliku ze spacjami.txt"
rm -f ".ukryty_plik.txt"